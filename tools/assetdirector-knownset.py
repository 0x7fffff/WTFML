#!/usr/bin/env python3
"""Regenerate the AssetDirector known-set manifest (GTNH first launch resources).

Modes:

  python tools/assetdirector-knownset.py --from-cache --ad-dir <root>/assets/asset_director
      --assets-root <root>/assets --tag <git tag> --mc-version <MC>        （换行只为阅读，实际一行跑完）
      **Preferred.** Derive the set from a verified AssetDirector cache: everything in that
      directory (each object re-checked against the official index by size + SHA-1) plus the
      previous manifest's objects that AssetDirector reused from the launcher's shared
      `assets/objects` instead of copying. A real GTNH 2.8.4 run proved the literal scan is
      only a lower bound: EFR also registers block sounds in a `for (ModSounds.CustomSound …)`
      loop, which the regex cannot see (612 missed objects).

  python tools/assetdirector-knownset.py --efr-java <EtFuturum.java> --tag <git tag>
      The static cross-check: the `ADConfig` literals + the matching Minecraft asset index +
      sounds.json. Its result must be a *subset* of the cache-derived set.
      Both modes write `assets/assetdirector/known-sets.json`.

  python tools/assetdirector-knownset.py --embed
      Re-emit `src/download/AssetDirectorKnownSetData.cpp` from the committed manifest.
      The embedded bytes and the JSON file must stay identical; the regression test
      `AssetDirectorKnownSetTests.cpp` asserts it.

This is a manual probe, not part of CTest: it needs the network and an EFR checkout.
See "MCLib AssetDirector 首启资源" in docs/features/MODPACK_IMPORT.md for why the set has to be
pinned per pack + mod version instead of being derived from the pack manifest.
"""
import argparse
import hashlib
import json
import os
import re
import sys
import urllib.request

REPO = __file__.replace("\\", "/").rsplit("/tools/", 1)[0]
MANIFEST = REPO + "/assets/assetdirector/known-sets.json"
EMBED = REPO + "/src/download/AssetDirectorKnownSetData.cpp"
MANIFEST_ENDPOINT = "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json"
RESOURCES_ENDPOINT = "https://resources.download.minecraft.net"
MAX_SET_BYTES = 256 * 1024 * 1024


def fetch(url, timeout=90):
    request = urllib.request.Request(url, headers={"User-Agent": "wtfml-knownset/1"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def fetch_json(url):
    return json.loads(fetch(url))


def resolve(efr_java, tag, mc_override=None):
    source = open(efr_java, encoding="utf-8").read()
    body = source.split("private static void getSounds(ADConfig config) {", 1)[1].split("\n\t}", 1)[0]
    if "config.addJar(ver)" in body:
        # A client jar would be ~29 MiB extra; that is a deliberate decision, not a silent add.
        sys.exit("EFR now requests a client jar; extend the manifest format first.")
    objects = re.findall(r'config\.addObject\(ver, "([^"]+)"\)', body)
    events = re.findall(r'config\.addSoundEvent\(ver, "([^"]+)"(?:, "([^"]*)")?\)', body)
    # Reference.MCAssetVer is generated from build.gradle's injectedTags; the caller passes it.
    mc = mc_override
    manifest = fetch_json(MANIFEST_ENDPOINT)
    entry = next((v for v in manifest["versions"] if v["id"] == mc), None)
    if entry is None:
        sys.exit("MC version %s is not in the Mojang version manifest." % mc)
    version_bytes = fetch(entry["url"])
    version_json = json.loads(version_bytes)
    index_ref = version_json["assetIndex"]
    index = fetch_json(index_ref["url"])["objects"]
    sound_json_ref = index.get("minecraft/sounds.json")
    if sound_json_ref is None:
        sys.exit("sounds.json is not part of the %s asset index." % mc)
    sounds = json.loads(fetch("%s/%s/%s" % (RESOURCES_ENDPOINT, sound_json_ref["hash"][:2], sound_json_ref["hash"])))

    names = set(objects)
    unknown = []
    for name, _category in events:
        event = sounds.get(name)
        if event is None:
            unknown.append(name)
            continue
        for sound in event["sounds"]:
            path = sound if isinstance(sound, str) else sound["name"]
            names.add("minecraft/sounds/" + path + ".ogg")
    names.add("minecraft/sounds.json")

    missing = sorted(n for n in names if n not in index)
    if missing:
        sys.exit("Not in the %s asset index: %s" % (mc, missing[:5]))
    items = [{"name": n, "hash": index[n]["hash"], "size": index[n]["size"]} for n in sorted(names)]
    if len({o["hash"] for o in items}) != len(items):
        sys.exit("Duplicate hashes in the resolved set.")
    total = sum(o["size"] for o in items)
    if total > MAX_SET_BYTES:
        sys.exit("Resolved set is %d bytes, above the confirmed cap." % total)

    escaped = re.escape(tag)
    document = {
        "formatVersion": 1,
        "generator": "MCLib AssetDirector literals + Mojang launchermeta, tag %s" % tag,
        "knownSets": [{
            "id": "gtnh-2.8.4-etfuturum" if tag.startswith("2.6.2.25") else "efr-" + tag.lower(),
            "note": "Et-Futurum-Requiem %s (MC asset version %s)" % (tag, mc),
            "consumerPatterns": [
                # GTNH 的 jar 名就是仓库名小写：etfuturum-2.6.2.25-GTNH.jar（实测 2.8.4 包里
                # 唯一那个 MCLib 消费者）。长短两种写法都留着：别的包/别的打包脚本会改。
                "^etfuturum-%s\\.jar$" % escaped,
                "(?i)^et[-_]?futurum(?:[-_]?requiem)?[-_]%s[-_]gtnh\\.jar$" % escaped,
            ],
            "minecraftVersion": mc,
            "versionJson": {"url": entry["url"], "sha1": hashlib.sha1(version_bytes).hexdigest(),
                            "size": len(version_bytes)},
            "assetIndex": {"id": index_ref["id"], "url": index_ref["url"],
                           "sha1": index_ref["sha1"], "size": index_ref["size"]},
            "totalBytes": total,
            "objects": items,
        }],
    }
    text = json.dumps(document, ensure_ascii=False, indent=1) + "\n"
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print("wrote %s: %d objects, %d bytes (%.2f MiB), %d sound events, %d object literals, unknown events %s"
          % (MANIFEST, len(items), total, total / 2 ** 20, len(events), len(objects), unknown))
    print("versionJson %s %s %d" % (entry["url"], hashlib.sha1(version_bytes).hexdigest(), len(version_bytes)))
    print("assetIndex %s %s %s %d" % (index_ref["id"], index_ref["url"], index_ref["sha1"], index_ref["size"]))
    print("remember to run --embed")


def embed():
    source = open(MANIFEST, encoding="utf-8").read()
    chunks = [source[i:i + 4000] for i in range(0, len(source), 4000)]
    lines = [
        "/**",
        " * @file AssetDirectorKnownSetData.cpp",
        " * @brief 生成文件，**不是手写的**：内容就是 `assets/assetdirector/known-sets.json` 的字节。",
        " *",
        " * 为什么生成而不是运行时读：产品没有\"装好的数据文件目录\"，而 qrc 里的资源在静态库里",
        " * 要靠 Q_INIT_RESOURCE 才稳。所以清单随源码编进来，人读的那一份留在",
        " * `assets/assetdirector/`；两份必须逐字节一致，验证工程会断言。",
        " *",
        " * 重新生成：`python tools/assetdirector-knownset.py --embed`",
        " * 别手改这个文件：下一次生成会把它整个盖掉。",
        " */",
        '#include "AssetDirectorKnownSet.hpp"',
        "",
        "namespace AssetDirectorKnownSet {",
        "namespace {",
        "constexpr const char *kChunks[] = {",
    ]
    for chunk in chunks:
        escaped = chunk.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
        lines.append('    "%s",' % escaped)
    lines += [
        "};",
        "}  // namespace",
        "",
        "QByteArray embeddedManifest() {",
        "    QByteArray data;",
        "    for (const char *chunk : kChunks) {",
        "        data.append(chunk);",
        "    }",
        "    return data;",
        "}",
        "",
        "}  // namespace AssetDirectorKnownSet",
        "",
    ]
    with open(EMBED, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("wrote %s: %d chunks, %d bytes" % (EMBED, len(chunks), len(source)))


def sha1_file(path, limit=None):
    import hashlib as _h
    digest = _h.sha1()
    read = 0
    with open(path, "rb") as handle:
        while True:
            chunk = handle.read(1 << 20)
            if not chunk:
                break
            digest.update(chunk)
            read += len(chunk)
            if limit is not None and read > limit:
                return None, read
    return digest.hexdigest(), read


def from_cache(ad_dir, assets_root, previous, tag, mc_version):
    """Derive the known set from a **verified cache** (the AssetDirector directory).

    Why this is the primary source: the static parse of EtFuturum.getSounds() misses the
    events registered in its `for (ModSounds.CustomSound sound : ...)` loop, so the literal
    scan is only a lower bound. A real GTNH 2.8.4 run showed 612 extra objects (block
    step/place/break sounds) that only the loop registers. The cache has exactly what
    AssetDirector asked for -- verify it against the official index and pin that.

    Union: everything in the AD directory + every object of the previous manifest that is
    present in the launcher's shared assets/objects (those are the ones AD *reused* instead
    of copying, so they never show up in its own directory).
    """
    indexes = [n for n in os.listdir(os.path.join(ad_dir, "assets", "indexes")) if n.endswith(".json")]
    if len(indexes) != 1:
        sys.exit("expected exactly one asset index in %s, found %s" % (ad_dir, indexes))
    index_doc = json.load(open(os.path.join(ad_dir, "assets", "indexes", indexes[0]), encoding="utf-8"))
    index = index_doc["objects"]
    by_hash = {o["hash"]: (name, o) for name, o in index.items()}

    def present(directory):
        found = set()
        for base, _dirs, files in os.walk(directory):
            for name in files:
                if len(name) == 40 and all(c in "0123456789abcdef" for c in name):
                    found.add(name)
        return found

    ad_hashes = present(os.path.join(ad_dir, "assets", "objects"))
    shared_hashes = present(os.path.join(assets_root, "objects"))
    prior = json.load(open(previous, encoding="utf-8"))["knownSets"][0]
    prior_hashes = {o["hash"] for o in prior["objects"]}
    reused = shared_hashes & prior_hashes
    union = ad_hashes | reused
    unknown = sorted(h for h in union if h not in by_hash)
    if unknown:
        sys.exit("objects not in the index: %s" % unknown[:5])

    items = []
    for h in sorted(union, key=lambda x: by_hash[x][0]):
        name, entry = by_hash[h]
        # 盘上那份必须真的是它：文件名 = 哈希还不够，AD 自己也不校验，所以这里按内容核一遍。
        path = os.path.join(ad_dir, "assets", "objects", h[:2], h) if h in ad_hashes \
            else os.path.join(assets_root, "objects", h[:2], h)
        digest, size = sha1_file(path)
        if digest != h or size != entry["size"]:
            sys.exit("cached object does not match the index: %s" % name)
        items.append({"name": name, "hash": h, "size": entry["size"]})
    total = sum(o["size"] for o in items)
    if total > MAX_SET_BYTES:
        sys.exit("resolved set is %d bytes, above the confirmed cap." % total)

    # URL/sha1 of the two JSON files come from launchermeta (not from the cached copies).
    manifest = fetch_json(MANIFEST_ENDPOINT)
    entry = next((v for v in manifest["versions"] if v["id"] == mc_version), None)
    if entry is None:
        sys.exit("MC version %s is not in the Mojang version manifest." % mc_version)
    version_bytes = fetch(entry["url"])
    version_json = json.loads(version_bytes)
    index_ref = version_json["assetIndex"]
    cached_version = os.path.join(ad_dir, "versions", mc_version, mc_version + ".json")
    cached_index = os.path.join(ad_dir, "assets", "indexes", indexes[0])
    if sha1_file(cached_version)[0] != hashlib.sha1(version_bytes).hexdigest():
        sys.exit("cached version json differs from launchermeta: %s" % cached_version)
    if sha1_file(cached_index)[0] != index_ref["sha1"]:
        sys.exit("cached asset index differs from launchermeta: %s" % cached_index)

    escaped = re.escape(tag)
    document = {
        "formatVersion": 1,
        "generator": "verified AssetDirector cache (GTNH run) + Mojang launchermeta, tag %s" % tag,
        "knownSets": [{
            "id": prior["id"],
            "note": prior["note"],
            "consumerPatterns": prior["consumerPatterns"],
            "minecraftVersion": mc_version,
            "versionJson": {"url": entry["url"], "sha1": hashlib.sha1(version_bytes).hexdigest(),
                            "size": len(version_bytes)},
            "assetIndex": {"id": index_ref["id"], "url": index_ref["url"],
                           "sha1": index_ref["sha1"], "size": index_ref["size"]},
            "totalBytes": total,
            "objects": items,
        }],
    }
    text = json.dumps(document, ensure_ascii=False, indent=1) + "\n"
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    added = union - prior_hashes
    dropped = prior_hashes - union
    print("wrote %s: %d objects, %d bytes (%.2f MiB)" % (MANIFEST, len(items), total, total / 2 ** 20))
    print("vs previous manifest: +%d / -%d (reused from shared objects: %d)"
          % (len(added), len(dropped), len(reused)))
    if dropped:
        print("dropped hashes: %s" % sorted(dropped)[:5])
    print("remember to run --embed")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--efr-java", help="path to EtFuturum.java of the pinned EFR tag")
    parser.add_argument("--tag", help="EFR git tag, e.g. 2.6.2.25-GTNH")
    parser.add_argument("--mc-version", help="MC asset version (default: from the tag's build.gradle)")
    parser.add_argument("--embed", action="store_true", help="re-emit the embedded C++ from the manifest")
    parser.add_argument("--from-cache", action="store_true",
                        help="derive the set from a verified AssetDirector cache instead of the literals")
    parser.add_argument("--ad-dir", help="<game root>/assets/asset_director of a verified run")
    parser.add_argument("--assets-root", help="<game root>/assets (for objects AssetDirector reused)")
    parser.add_argument("--previous", default=MANIFEST, help="manifest whose reused objects count as wanted")
    args = parser.parse_args()
    if args.embed:
        embed()
        return
    if args.from_cache:
        if not (args.ad_dir and args.assets_root and args.tag and args.mc_version):
            parser.error("--from-cache needs --ad-dir, --assets-root, --tag and --mc-version")
        from_cache(args.ad_dir, args.assets_root, args.previous, args.tag, args.mc_version)
        return
    if not args.efr_java or not args.tag or not args.mc_version:
        parser.error("--efr-java, --tag and --mc-version are required to regenerate the manifest")
    resolve(args.efr_java, args.tag, args.mc_version)


if __name__ == "__main__":
    main()
