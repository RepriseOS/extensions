import contextlib
import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import catalog


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name)
        self.sources = {
            k: v
            for k, v in catalog.registry(catalog.ROOT / "catalog/sources.toml").items()
            if v["repo"] == catalog.FIRST_PARTY
        }
        self.packages = catalog.ROOT / "build/Extensions"

    def preview(self, identifier, source, old):
        return catalog.preview(identifier, source, old, packages=self.packages)

    def entries(self):
        return {
            e["id"]: e
            for e in json.loads((self.out / "catalog-v1.json").read_text())[
                "extensions"
            ]
        }

    def test_preview_packages_and_referenced_images(self):
        self.assertEqual(catalog.generate(self.sources, self.out, self.preview), [])
        entries = self.entries()
        self.assertEqual(set(entries), set(self.sources))
        for entry in entries.values():
            assets = [entry["package"]]
            if entry["category"] != "library":
                self.assertTrue(entry["screenshots"][0]["alt"])
                self.assertEqual(entry["screenshots"][0]["width"], 320)
                assets += [entry["icon"], *entry["screenshots"]]
            for asset in assets:
                data = (self.out / asset["url"]).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), asset["sha256"])
                self.assertEqual(len(data), asset["size"])
        self.assertEqual(entries["crossfeed"]["requires"], ["dsp"])

    @contextlib.contextmanager
    def balance_release(self, cores=(("0.4.0", 0x10052),), listing=b""):
        source = self.sources["balance"]
        repo = source["repo"]
        folder = catalog.ROOT / source["path"]
        package = (self.packages / "balance.rvx").read_bytes()
        release = {
            "tag_name": "balance/v0.1.0",
            "published_at": "2026-11-24T00:00:00Z",
            "assets": [
                {"name": "balance.rvx", "size": len(package), "state": "uploaded"}
            ],
        }

        def download(url, limit):
            if url.endswith(".rvx"):
                return package
            base = f"https://raw.githubusercontent.com/{repo}/{'a' * 40}/extensions/balance/"
            self.assertTrue(url.startswith(base))
            data = (folder / url.removeprefix(base)).read_bytes()
            return listing + data if url.endswith("/listing.toml") else data

        with (
            patch.object(catalog, "releases", return_value=[release]),
            patch.object(catalog, "core_releases", return_value=list(cores)),
            patch.object(catalog, "api_json", return_value={"sha": "a" * 40}),
            patch.object(catalog, "read_url", side_effect=download),
        ):
            yield release

    def test_remote_release_and_failed_update_retains_all_assets(self):
        sources = {"balance": self.sources["balance"]}
        with self.balance_release() as release:
            self.assertEqual(catalog.generate(sources, self.out, catalog.remote), [])
            before = self.entries()
            self.assertEqual(before["balance"]["min_repriseos"], "0.4.0")
            release["tag_name"] = "balance/v0.1.1"
            self.assertTrue(catalog.generate(sources, self.out, catalog.remote))
            self.assertEqual(before, self.entries())
            self.assertTrue((self.out / before["balance"]["package"]["url"]).exists())

    def test_update_waits_for_a_core_release(self):
        sources = {"balance": self.sources["balance"]}
        with self.balance_release(cores=[("0.4.0", 0x10040)]):
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                self.assertEqual(
                    catalog.generate(sources, self.out, catalog.remote), []
                )
            self.assertEqual(self.entries(), {})
            self.assertIn("balance 0.1.0 waits for", stderr.getvalue())

    def test_held_back_update_keeps_the_published_entry(self):
        sources = {"balance": self.sources["balance"]}
        with self.balance_release():
            self.assertEqual(catalog.generate(sources, self.out, catalog.remote), [])
        index = self.out / "catalog-v1.json"
        index.write_text(index.read_text().replace('"0.1.0"', '"0.0.9"'))
        before = self.entries()
        with self.balance_release(cores=[("0.4.0", 0x10040)]):
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(
                    catalog.generate(sources, self.out, catalog.remote), []
                )
        self.assertEqual(self.entries(), before)

    def test_kept_entry_of_a_waiting_dependent_gains_the_derived_release(self):
        sources = {"lib": {"repo": "creator/lib"}, "player": {"repo": "creator/player"}}
        published = {
            "id": "player",
            "version": "1.0.0",
            "api": "1.78",
            "category": "media",
            "requires": [],
            "repo": "creator/player",
            "source_path": "",
            "tag_prefix": "v",
            "release": {"tag": "v1.0.0"},
        }
        index = self.out / "catalog-v1.json"
        index.write_text(json.dumps({"schema_version": 1, "extensions": [published]}))

        def fetch(identifier, source, old):
            entry = dict(published, version="1.1.0", requires=["lib"], id=identifier)
            if identifier == "lib":
                raise catalog.HeldBack(
                    "lib 1.0.0 waits", dict(entry, category="library")
                )
            return entry, {}

        with (
            patch.object(catalog, "core_releases", return_value=[("0.4.0", 0x10052)]),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            self.assertEqual(catalog.generate(sources, self.out, fetch), [])
        player = self.entries()["player"]
        self.assertEqual(
            (player["version"], player["min_repriseos"]), ("1.0.0", "0.4.0")
        )

    def test_published_entries_gain_the_derived_release(self):
        sources = {"balance": self.sources["balance"]}
        with self.balance_release():
            self.assertEqual(catalog.generate(sources, self.out, catalog.remote), [])
            index = self.out / "catalog-v1.json"
            document = json.loads(index.read_text())
            del document["extensions"][0]["min_repriseos"]
            index.write_text(json.dumps(document))
            self.assertEqual(catalog.generate(sources, self.out, catalog.remote), [])
        self.assertEqual(self.entries()["balance"]["min_repriseos"], "0.4.0")

    def test_dependents_of_a_held_back_library_wait_too(self):
        sources = {"lib": {"repo": "creator/lib"}, "player": {"repo": "creator/player"}}

        def fetch(identifier, source, old):
            entry = {
                "id": identifier,
                "version": "1.0.0",
                "category": "library" if identifier == "lib" else "media",
                "requires": [] if identifier == "lib" else ["lib"],
            }
            if identifier == "lib":
                raise catalog.HeldBack("lib 1.0.0 waits", entry)
            return entry, {}

        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(catalog.generate(sources, self.out, fetch), [])
        self.assertEqual(self.entries(), {})
        self.assertIn("player waits for libraries ['lib']", stderr.getvalue())

    def test_a_library_chain_waits_for_its_held_back_end(self):
        sources = {k: {"repo": f"creator/{k}"} for k in ("b", "a", "lib")}
        needs = {"b": ["a"], "a": ["lib"], "lib": []}

        def fetch(identifier, source, old):
            entry = {
                "id": identifier,
                "version": "1.0.0",
                "category": "media" if identifier == "b" else "library",
                "requires": needs[identifier],
            }
            if identifier == "lib":
                raise catalog.HeldBack("lib 1.0.0 waits", entry)
            return entry, {}

        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(catalog.generate(sources, self.out, fetch), [])
        self.assertEqual(self.entries(), {})

    def test_submission_accepts_a_release_waiting_for_a_core(self):
        entry = {"category": "media", "requires": [], "version": "1.0.0"}
        entry["package"] = {"sha256": "verified"}
        held = catalog.HeldBack("player 1.0.0 waits", entry)
        with (
            patch.object(catalog, "remote", side_effect=held),
            contextlib.redirect_stdout(io.StringIO()) as stdout,
        ):
            catalog.review({"player": {"repo": "creator/player"}}, {})
        self.assertIn("player 1.0.0 waits", stdout.getvalue())

    def test_listing_can_raise_the_derived_release(self):
        sources = {"balance": self.sources["balance"]}
        for listing, expected in (
            (b'min_repriseos = "0.9.0"\n', "0.9.0"),
            (b'min_repriseos = "0.1.0"\n', "0.4.0"),
        ):
            with self.subTest(listing=listing), self.balance_release(listing=listing):
                (self.out / "catalog-v1.json").unlink(missing_ok=True)
                self.assertEqual(
                    catalog.generate(sources, self.out, catalog.remote), []
                )
                self.assertEqual(self.entries()["balance"]["min_repriseos"], expected)

    def test_core_releases_read_the_api_at_each_tag(self):
        tags = ["v0.5.0", "sdk/v1.84.0", "v0.4.0", "v0.3.1", "v0.4.1-beta"]
        headers = {
            "v0.4.0": b"RVX_API_MAJOR = 1,\n    RVX_API = (RVX_API_MAJOR << 16) | 82\n",
            "v0.5.0": b"RVX_API_MAJOR = 1,\n    RVX_API = (RVX_API_MAJOR << 16) | 84\n",
        }

        def download(url, limit):
            tag = url.split("/")[5].replace("%2F", "/")
            if tag not in headers:
                raise HTTPError(url, 404, "Not Found", None, None)
            return headers[tag]

        with (
            patch.object(
                catalog, "releases", return_value=[{"tag_name": t} for t in tags]
            ),
            patch.object(catalog, "read_url", side_effect=download),
        ):
            cores = catalog.core_releases.__wrapped__()
        self.assertEqual(cores, [("0.4.0", 0x10052), ("0.5.0", 0x10054)])
        with patch.object(catalog, "core_releases", return_value=cores):
            self.assertEqual(catalog.first_release(0x1004E), "0.4.0")
            self.assertEqual(catalog.first_release(0x10053), "0.5.0")
            self.assertIsNone(catalog.first_release(0x10055))
            self.assertIsNone(catalog.first_release(0x20000))

    def test_missing_dependency_and_unregistered_extension(self):
        self.assertEqual(catalog.generate(self.sources, self.out, self.preview), [])
        del self.sources["dsp"]
        self.assertTrue(catalog.generate(self.sources, self.out, self.preview))
        self.assertNotIn("crossfeed", self.entries())
        self.assertNotIn("dsp", self.entries())

    def test_newest_is_numeric_and_per_extension(self):
        releases = [
            {"tag_name": tag}
            for tag in (
                "balance/v0.9.0",
                "dsp/v3.0.0",
                "balance/v0.10.0",
                "balance/v1.0.0-beta",
            )
        ]
        with patch.object(catalog, "releases", return_value=releases):
            self.assertEqual(
                catalog.newest(self.sources["balance"])["tag_name"], "balance/v0.10.0"
            )

    def test_submission_template_requires_description_and_matching_links(self):
        body = (
            "- Extension and what it does: A listening timer.\n"
            "- Source repository: https://github.com/example/timer\n"
            "- Stable release with the .rvx: https://github.com/example/timer/releases/tag/v1.0.0"
        )
        catalog.submission_body(body, "example/timer")
        with self.assertRaisesRegex(ValueError, "source repository"):
            catalog.submission_body(body, "another/timer")
        with self.assertRaisesRegex(ValueError, "tagged release"):
            catalog.submission_body(body.replace("/tag/v1.0.0", ""), "example/timer")
        with self.assertRaisesRegex(ValueError, "description"):
            catalog.submission_body("<!-- " + body + " -->", "example/timer")

    def test_submission_checks_changed_external_sources_only(self):
        sources = {
            "existing": {"repo": "creator/existing"},
            "new": {"repo": "creator/new"},
        }
        previous = {"existing": sources["existing"]}
        with patch.object(catalog, "remote", return_value=None) as remote:
            with self.assertRaisesRegex(ValueError, "stable release"):
                catalog.review(sources, previous)
            remote.assert_called_once_with("new", sources["new"], None)
        with (
            patch.object(
                catalog, "remote", return_value=({"requires": ["missing"]}, {})
            ),
            self.assertRaisesRegex(ValueError, "unavailable libraries"),
        ):
            catalog.review(sources, previous)

    def test_submission_uses_published_library_categories(self):
        self.assertEqual(catalog.generate(self.sources, self.out, self.preview), [])
        sources = {**self.sources, "player": {"repo": "creator/player"}}
        entry = {
            "category": "media",
            "requires": ["balance"],
            "version": "1.0.0",
            "package": {"sha256": "verified"},
        }
        with patch.object(catalog, "remote", return_value=(entry, {})) as remote:
            index = self.out / "catalog-v1.json"
            with self.assertRaisesRegex(ValueError, "unavailable libraries.*balance"):
                catalog.review(sources, self.sources, index=index)
            entry["requires"] = ["dsp"]
            catalog.review(sources, self.sources, index=index)
            self.assertEqual(
                [call.args[0] for call in remote.call_args_list], ["player", "player"]
            )

    def test_submission_accepts_library_from_same_pr_regardless_of_order(self):
        sources = {"player": {"repo": "creator/player"}, "lib": {"repo": "creator/lib"}}

        def release(identifier, source, old):
            return (
                {
                    "category": "library" if identifier == "lib" else "media",
                    "requires": [] if identifier == "lib" else ["lib"],
                    "version": "1.0.0",
                    "package": {"sha256": "verified"},
                },
                {},
            )

        with patch.object(catalog, "remote", side_effect=release) as remote:
            catalog.review(sources, {})
            self.assertEqual(remote.call_count, 2)

    def test_corrupt_package_is_rejected(self):
        source = self.sources["balance"]
        folder = catalog.ROOT / source["path"]
        package = bytearray((self.packages / "balance.rvx").read_bytes())
        package[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            catalog.snapshot(
                "balance",
                source,
                "0.1.0",
                lambda path: (folder / path).read_bytes(),
                package,
                None,
            )

    def test_image_path_stays_in_release_source(self):
        for value in (
            "../another/file.png",
            "/etc/passwd",
            "https://elsewhere/icon.png",
        ):
            with self.assertRaises(ValueError):
                catalog.relative(value)
