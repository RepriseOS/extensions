"""Build the static RVX catalog from reviewed sources and tagged release assets."""

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import tomllib
from functools import cache, partial
from pathlib import Path
from urllib.error import URLError
from urllib.parse import quote
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(
    0, str(Path(os.environ.get("SDK", ROOT.parent / "osos-lab/rvx-sdk")).resolve())
)
import manifest
import rvx

VERSION = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", re.ASCII)
REPO = re.compile(r"[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+")
FIRST_PARTY = "RepriseOS/repriseos-extensions"
MIB = 1024 * 1024


def relative(value):
    if not isinstance(value, str) or not all(
        re.fullmatch(r"[a-zA-Z0-9_-][a-zA-Z0-9_.-]*", part) for part in value.split("/")
    ):
        raise ValueError(f"expected a relative repository path: {value!r}")
    return value


def registry(path):
    sources = tomllib.loads(path.read_text())
    for identifier, source in sources.items():
        if (
            not manifest.ID_PATTERN.fullmatch(identifier)
            or identifier in manifest.RESERVED_IDS
        ):
            raise ValueError(f"invalid extension ID: {identifier}")
        if not isinstance(source, dict) or set(source) - {"repo", "path", "tag_prefix"}:
            raise ValueError(f"invalid source: {identifier}")
        if not REPO.fullmatch(source.get("repo", "")) or source["repo"].split("/")[
            1
        ] in {".", ".."}:
            raise ValueError(f"invalid GitHub repository: {identifier}")
        if "path" in source:
            relative(source["path"])
        relative(source.get("tag_prefix", "v"))
    return sources


def read_url(url, limit, api=False):
    headers = {"User-Agent": "RepriseOS-extension-catalog"}
    if api and os.environ.get("GITHUB_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GITHUB_TOKEN"]
    with urlopen(Request(url, headers=headers), timeout=30) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"file exceeds {limit} bytes: {url}")
    return data


def api_json(repo, path):
    return json.loads(
        read_url(f"https://api.github.com/repos/{repo}/{path}", 16 * MIB, api=True)
    )


@cache
def releases(repo):
    result = []
    page = 1
    while True:
        batch = api_json(repo, f"releases?per_page=100&page={page}")
        result.extend(r for r in batch if not r["draft"] and not r["prerelease"])
        if len(batch) < 100:
            return result
        page += 1


def version_key(version):
    if not isinstance(version, str) or not VERSION.fullmatch(version):
        raise ValueError(f"expected a stable x.y.z version: {version!r}")
    return tuple(map(int, version.split(".")))


def newest(source):
    prefix = source.get("tag_prefix", "v")
    candidates = {}
    for release in releases(source["repo"]):
        tag = release["tag_name"]
        if tag.startswith(prefix) and VERSION.fullmatch(tag[len(prefix) :]):
            candidates[version_key(tag[len(prefix) :])] = release
    return candidates[max(candidates)] if candidates else None


def required_text(value, name):
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"missing text for {name}")
    return value


def snapshot(identifier, source, version, read_file, package, release):
    doc = tomllib.loads(read_file("manifest.toml").decode())
    if doc["id"] != identifier or doc["version"] != version:
        raise ValueError("release tag, registered ID and manifest must agree")
    version_key(version)
    compiled = manifest.compile_manifest(doc)
    header, _ = rvx.unpack(package)
    if (
        header["manifest"] != len(compiled)
        or package[rvx.HEADER.size : rvx.HEADER.size + len(compiled)] != compiled
    ):
        raise ValueError("package contains a different manifest")
    if rvx.fnv1a(package[rvx.HEADER.size :]) != header["body_hash"]:
        raise ValueError("package body checksum mismatch")
    api = manifest.api_word(doc["api"])
    if header["api"] >> 16 != api >> 16 or header["api"] < api:
        raise ValueError("package and manifest API mismatch")

    listing = tomllib.loads(read_file("listing.toml").decode())
    allowed = {
        "details",
        "icon",
        "screenshots",
        "author_handle",
        "min_repriseos",
        "models",
    }
    if listing.keys() - allowed:
        raise ValueError(f"unknown listing fields: {listing.keys() - allowed}")

    files = {}

    def asset(name, data):
        digest = hashlib.sha256(data).hexdigest()
        url = f"assets/{identifier}/{digest}/{name}"
        files[url] = data
        return {"url": url, "sha256": digest, "size": len(data)}

    def image(filename):
        data = read_file(relative(filename))
        if (
            not filename.lower().endswith(".png")
            or len(data) < 33
            or data[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR"
            or data[-12:] != b"\x00\x00\x00\x00IEND\xaeB\x60\x82"
        ):
            raise ValueError(f"expected a PNG image: {filename}")
        width, height = struct.unpack_from(">II", data, 16)
        if not (0 < width <= 4096 and 0 < height <= 4096):
            raise ValueError(f"unsupported image dimensions: {filename}")
        return dict(asset(Path(filename).name, data), width=width, height=height)

    entry = {field: doc[field] for field in manifest.TEXT_FIELDS}
    entry.update(
        category=manifest.category(doc),
        api=doc["api"],
        requires=manifest.libraries(doc)[0],
        firmware_id=f"{header['firmware']:08x}",
        repo=source["repo"],
        source_path=source.get("path", ""),
        tag_prefix=source.get("tag_prefix", "v"),
        details=required_text(listing["details"], "details"),
        release=release,
        package=asset(identifier + ".rvx", package),
    )
    for field in ("stability", "note"):
        if field in doc:
            entry[field] = doc[field]
    if "author_handle" in listing:
        if not re.fullmatch(r"[\w-]+", listing["author_handle"], re.ASCII):
            raise ValueError("invalid GitHub author handle")
        entry["author_handle"] = listing["author_handle"]
    if "min_repriseos" in listing:
        version_key(listing["min_repriseos"])
        entry["min_repriseos"] = listing["min_repriseos"]
    if "models" in listing:
        models = listing["models"]
        if not isinstance(models, list) or not all(
            isinstance(m, str) and m.strip() for m in models
        ):
            raise ValueError("models must be a list of iPod model names")
        entry["models"] = models
    icon = listing.get("icon") or next(
        (a["file"] for a in doc.get("asset", []) if a.get("pane") == "icon"), None
    )
    if icon:
        entry["icon"] = image(icon)
    entry["screenshots"] = [
        dict(image(shot["file"]), alt=required_text(shot["alt"], "screenshot alt"))
        for shot in listing.get("screenshots", [])
    ]
    return entry, files


def remote(identifier, source, old):
    release = newest(source)
    if not release:
        return None
    tag = release["tag_name"]
    version = tag[len(source.get("tag_prefix", "v")) :]
    if old and version_key(version) <= version_key(old["version"]):
        return None
    repo = source["repo"]
    escaped_tag = quote(tag, safe="")
    filename = identifier + ".rvx"
    assets = [
        a
        for a in release["assets"]
        if a["name"] == filename and a["state"] == "uploaded"
    ]
    if len(assets) != 1 or not 0 < assets[0]["size"] <= 16 * MIB:
        raise ValueError(f"{tag} needs a release asset named {filename} (max 16 MiB)")
    sha = api_json(repo, f"commits/{escaped_tag}")["sha"]
    if not re.fullmatch("[0-9a-f]{40}", sha):
        raise ValueError("invalid source revision")
    base = f"https://raw.githubusercontent.com/{repo}/{sha}/"
    if "path" in source:
        base += source["path"] + "/"
    package = read_url(
        f"https://github.com/{repo}/releases/download/{escaped_tag}/{filename}",
        16 * MIB,
    )
    info = {
        "tag": tag,
        "url": f"https://github.com/{repo}/releases/tag/{escaped_tag}",
        "commit": sha,
        "published_at": release["published_at"],
    }
    return snapshot(
        identifier,
        source,
        version,
        lambda name: read_url(base + name, 4 * MIB),
        package,
        info,
    )


def preview(identifier, source, old, *, packages):
    folder = ROOT / source.get("path", "")
    version = tomllib.loads((folder / "manifest.toml").read_text())["version"]
    return snapshot(
        identifier,
        source,
        version,
        lambda name: (folder / name).read_bytes(),
        (packages / (identifier + ".rvx")).read_bytes(),
        None,
    )


def published_entries(sources, index):
    old_entries = (
        json.loads(index.read_text())["extensions"] if index and index.exists() else []
    )
    old = {e["id"]: e for e in old_entries if e["id"] in sources}
    for identifier, entry in list(old.items()):
        source = sources[identifier]
        if (entry["repo"], entry["source_path"], entry["tag_prefix"]) != (
            source["repo"],
            source.get("path", ""),
            source.get("tag_prefix", "v"),
        ):
            del old[identifier]
    return old


def unavailable_libraries(entry, entries):
    return [
        d for d in entry["requires"] if entries.get(d, {}).get("category") != "library"
    ]


def generate(sources, out, fetch):
    index = out / "catalog-v1.json"
    old = published_entries(sources, index)
    entries = dict(old)
    pending = {}
    errors = []
    for identifier, source in sources.items():
        try:
            update = fetch(identifier, source, old.get(identifier))
            if update:
                if (
                    old.get(identifier, {}).get("category") == "library"
                    and update[0]["category"] != "library"
                ):
                    raise ValueError("a published library must keep its category")
                entries[identifier], pending[identifier] = update
        except (
            ValueError,
            TypeError,
            KeyError,
            IndexError,
            AttributeError,
            OverflowError,
            struct.error,
            URLError,
            OSError,
        ) as exc:
            errors.append(f"{identifier}: {exc}")

    for identifier, entry in list(entries.items()):
        missing = unavailable_libraries(entry, entries)
        if missing:
            errors.append(f"{identifier}: unavailable libraries {missing}")
            pending.pop(identifier, None)
            previous = old.get(identifier)
            if previous and not unavailable_libraries(previous, entries):
                entries[identifier] = previous
            else:
                del entries[identifier]

    for files in pending.values():
        for name, data in files.items():
            path = out / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    out.mkdir(parents=True, exist_ok=True)
    catalog = {"schema_version": 1, "extensions": [entries[k] for k in sorted(entries)]}
    tmp = index.with_suffix(".tmp")
    tmp.write_text(json.dumps(catalog, indent=2, ensure_ascii=False) + "\n")
    tmp.replace(index)
    return errors


def submission_body(body, repo):
    body = re.sub(r"<!--.*?-->", "", body, flags=re.DOTALL)
    fields = {}
    for line in body.splitlines():
        label, separator, value = line.strip().removeprefix("- ").partition(":")
        if separator:
            fields[label.strip().lower()] = value.strip()
    if not fields.get("extension and what it does"):
        raise ValueError("fill in the extension description in the PR template")
    url = re.escape("https://github.com/" + repo)
    if not re.search(
        url + r"/?(?=[)\s,]|$)", fields.get("source repository", ""), re.IGNORECASE
    ):
        raise ValueError(f"link the source repository {repo} in the PR template")
    if not re.search(
        url + r"/releases/tag/[^ )]+",
        fields.get("stable release with the .rvx", ""),
        re.IGNORECASE,
    ):
        raise ValueError(f"link a tagged release from {repo} in the PR template")


def local_manifest(identifier, source):
    folder = ROOT / source.get("path", "")
    doc = tomllib.loads((folder / "manifest.toml").read_text())
    if doc["id"] != identifier or not (folder / "listing.toml").is_file():
        raise ValueError(f"{identifier}: expected matching local manifest and listing")
    return {"category": manifest.category(doc), "requires": manifest.libraries(doc)[0]}


def review(sources, previous, body=None, index=None):
    entries = published_entries(sources, index)
    proposed = {}
    local = {}
    for identifier, source in sources.items():
        if previous.get(identifier) == source:
            continue
        if source["repo"] == FIRST_PARTY:
            local[identifier] = local_manifest(identifier, source)
            proposed[identifier] = local[identifier]
            continue
        if identifier not in previous and body is not None:
            submission_body(body, source["repo"])
        release = remote(identifier, source, None)
        if not release:
            raise ValueError(
                f"{identifier}: publish a stable release before submitting"
            )
        entry, _ = release
        if (
            entries.get(identifier, {}).get("category") == "library"
            and entry["category"] != "library"
        ):
            raise ValueError(
                f"{identifier}: a published library must keep its category"
            )
        proposed[identifier] = entries[identifier] = entry

    for identifier, entry in proposed.items():
        for dep in entry["requires"]:
            if dep not in sources:
                continue
            if identifier in local and sources[dep]["repo"] == FIRST_PARTY:
                if dep not in local:
                    local[dep] = local_manifest(dep, sources[dep])
            elif dep not in entries:
                release = remote(dep, sources[dep], None)
                if release:
                    entries[dep] = release[0]
        available = entries | local if identifier in local else entries
        missing = unavailable_libraries(entry, available)
        if missing:
            raise ValueError(f"{identifier}: unavailable libraries {missing}")
        if identifier not in local:
            print(
                f"Verified {identifier} {entry['version']} ({entry['package']['sha256']})"
            )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["preview", "sync", "review"])
    parser.add_argument("--out", type=Path)
    parser.add_argument("--packages", type=Path, default=ROOT / "build/Extensions")
    parser.add_argument("--base-registry", type=Path)
    parser.add_argument("--pr-event", type=Path)
    parser.add_argument("--published-index", type=Path)
    args = parser.parse_args()
    sources = registry(ROOT / "catalog/sources.toml")
    if args.mode == "review":
        previous = registry(args.base_registry) if args.base_registry else {}
        body = None
        if args.pr_event:
            body = (
                json.loads(args.pr_event.read_text())["pull_request"].get("body") or ""
            )
        review(sources, previous, body, args.published_index)
        return 0
    fetch = remote
    if args.mode == "preview":
        sources = {k: v for k, v in sources.items() if v["repo"] == FIRST_PARTY}

        fetch = partial(preview, packages=args.packages)

    default_out = "catalog" if args.mode == "preview" else "published"
    errors = generate(sources, args.out or ROOT / "build" / default_out, fetch)
    for error in errors:
        print(error, file=sys.stderr)
    return 2 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
