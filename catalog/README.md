# Catalog submissions

Keep your extension in your own public GitHub repository. Open one PR adding its ID to [sources.toml](sources.toml). For example:

```toml
[my-extension]
repo = "your-name/my-extension"
```

In the PR, briefly describe the extension and link the source and release. The submission check validates the registry format, the tagged manifests, the package, its dependencies and its images.

Publish a stable GitHub Release tagged `v1.0.0` with a `my-extension.rvx` asset. The tagged source must contain the matching SDK `manifest.toml` and a `listing.toml` alongside it. For monorepos, set `path` and `tag_prefix` as in the first-party entries. Publish a higher version for subsequent updates, including artwork or copy changes.

```toml
details = "Explain what the extension does and where to find it on the iPod."

[[screenshots]]
file = "screenshots/settings.png"
alt = "Describe the screen and the setting shown."
```

Screenshots are optional. Use PNGs relative to the listing. The catalog uses the manifest's pane icon unless the listing sets `icon`. Optional listing fields include `author_handle` (GitHub username), `min_repriseos` (x.y.z) and `models` (iPod model names). Omit unknown compatibility values. The catalog sets `min_repriseos` to the first RepriseOS release that supports the manifest's `api`; a listing value only raises it.

## Publishing

The scheduled Action scans approved sources about every five hours. It checks the released RVX against the tagged manifest and copies the verified package and images to the `catalog` branch. Invalid updates keep the previous valid version and appear in the workflow log. A release whose `api` is newer than every RepriseOS release also keeps the previous version and is published once a RepriseOS release supports it. Removing a source from the registry removes its listing. The installer and site can read `catalog-v1.json` from that branch and resolve asset URLs relative to it. A configured `SITE_DEPLOY_HOOK` requests a site rebuild on changes.

For the extensions in this repo, a tag such as `balance/v0.1.0` builds and publishes the matching package. To preview the listings locally with the SDK and ARM compiler available:

```sh
make -j8 PYTHON=python3.11 CROSS_COMPILE="$PWD/../osos-lab/toolchains/rockbox-arm/bin/arm-elf-eabi-"
python3.11 tools/catalog.py preview
```

Open `build/catalog/catalog-v1.json` for the preview. Preview entries use local builds and have no release link.
