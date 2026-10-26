#!/usr/bin/env python3
"""Regenerate the AssetDirector known-set manifest (GTNH first launch resources).

Two modes:

  python tools/assetdirector-knownset.py --efr-java <EtFuturum.java> --tag <git tag>
      Resolve what MCLib's AssetDirector will request for that Et Futurum Requiem build
      (its `ADConfig` literals + the matching Minecraft asset index + sounds.json) and
      write `assets/assetdirector/known-sets.json`.

  python tools/assetdirector-knownset.py --embed
      Re-emit `src/download/AssetDirectorKnownSetData.cpp` from the committed manifest.
      The embedded bytes and the JSON file must stay identical; the regression test
      `AssetDirectorKnownSetTests.cpp` asserts it.

This is a manual probe, not part of CTest: it needs the network and an EFR checkout.
See "MCLib AssetDirector 首启资源" in docs/MODPACK_IMPORT.md for why the set has to be
pinned per pack + mod version instead of being derived from the pack manifest.
"""
import argparse
import hashlib
import json
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
                "^Et-Futurum-Requiem-%s\\.jar$" % escaped,
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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--efr-java", help="path to EtFuturum.java of the pinned EFR tag")
    parser.add_argument("--tag", help="EFR git tag, e.g. 2.6.2.25-GTNH")
    parser.add_argument("--mc-version", help="MC asset version (default: from the tag's build.gradle)")
    parser.add_argument("--embed", action="store_true", help="re-emit the embedded C++ from the manifest")
    args = parser.parse_args()
    if args.embed:
        embed()
        return
    if not args.efr_java or not args.tag or not args.mc_version:
        parser.error("--efr-java, --tag and --mc-version are required to regenerate the manifest")
    resolve(args.efr_java, args.tag, args.mc_version)


if __name__ == "__main__":
    main()
