#!/usr/bin/env python3
"""Synchronize SquareStar SBOM dependency metadata with repository manifests/pins."""
from __future__ import annotations

import argparse
import json
import re
from datetime import datetime, timezone
from pathlib import Path


def load(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def save(path: Path, data) -> None:
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def fetch_content_pin(cmake: str, target: str) -> tuple[str, str]:
    pattern = re.compile(
        rf"FetchContent_Declare\(\s*{re.escape(target)}\s+"
        rf".*?\bURL\s+(\S+)\s+"
        rf"URL_HASH\s+SHA256=([0-9a-fA-F]{{64}})",
        re.S,
    )
    match = pattern.search(cmake)
    if not match:
        raise RuntimeError(f"could not find pinned FetchContent metadata for {target}")
    return match.group(1), match.group(2).lower()


def dynamic_dependency_metadata(cmake: str) -> dict[str, dict[str, str]]:
    glfw_url, glfw_hash = fetch_content_pin(cmake, "glfw")
    curl_url, curl_hash = fetch_content_pin(cmake, "curl")

    glfw_version_match = re.search(r"/([0-9]+(?:\.[0-9]+)+)\.tar\.gz$", glfw_url)
    curl_version_match = re.search(r"curl-([0-9]+(?:\.[0-9]+)+)\.tar\.(?:gz|xz)$", curl_url)
    if not glfw_version_match or not curl_version_match:
        raise RuntimeError("could not derive GLFW/curl versions from pinned archive URLs")

    glfw_version = glfw_version_match.group(1)
    curl_version = curl_version_match.group(1)
    return {
        "GLFW": {
            "version": glfw_version,
            "url": glfw_url,
            "sha256": glfw_hash,
            "purl": f"pkg:github/glfw/glfw@{glfw_version}",
        },
        "curl": {
            "version": curl_version,
            "url": curl_url,
            "sha256": curl_hash,
            "purl": f"pkg:generic/curl@{curl_version}",
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--timestamp", help="ISO-8601 UTC timestamp; defaults to current UTC")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    timestamp = args.timestamp or datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")

    manifest = load(root / "THIRD_PARTY_MANIFEST.json")
    vendored_hashes = {x["name"]: x["treeSha256"] for x in manifest["components"]}
    dynamic = dynamic_dependency_metadata((root / "cmake/SquareStarMain.cmake").read_text(encoding="utf-8"))

    spdx = load(root / "SBOM.spdx.json")
    spdx.setdefault("creationInfo", {})["created"] = timestamp
    spdx["creationInfo"]["creators"] = ["Tool: scripts/refresh-sbom.py"]
    for package in spdx.get("packages", []):
        name = package.get("name")
        if name in vendored_hashes:
            package["checksums"] = [{"algorithm": "SHA256", "checksumValue": vendored_hashes[name]}]
        if name in dynamic:
            dep = dynamic[name]
            package["versionInfo"] = dep["version"]
            package["downloadLocation"] = dep["url"]
            package["checksums"] = [{"algorithm": "SHA256", "checksumValue": dep["sha256"]}]
            for ref in package.get("externalRefs", []):
                if ref.get("referenceType") == "purl":
                    ref["referenceLocator"] = dep["purl"]
    save(root / "SBOM.spdx.json", spdx)

    cdx = load(root / "SBOM.cdx.json")
    cdx.setdefault("metadata", {})["timestamp"] = timestamp
    cdx["metadata"]["tools"] = {"components": [{"type": "application", "name": "scripts/refresh-sbom.py"}]}

    old_to_new_refs: dict[str, str] = {}
    for component in cdx.get("components", []):
        name = component.get("name")
        if name in vendored_hashes:
            component["hashes"] = [{"alg": "SHA-256", "content": vendored_hashes[name]}]
        if name in dynamic:
            dep = dynamic[name]
            old_ref = component.get("bom-ref", dep["purl"])
            old_to_new_refs[str(old_ref)] = dep["purl"]
            component["bom-ref"] = dep["purl"]
            component["version"] = dep["version"]
            component["purl"] = dep["purl"]
            component["hashes"] = [{"alg": "SHA-256", "content": dep["sha256"]}]
            refs = component.get("externalReferences", [])
            website = next((r for r in refs if r.get("type") == "website"), None)
            if website is None:
                refs.append({"type": "website", "url": dep["url"]})
            else:
                website["url"] = dep["url"]
            component["externalReferences"] = refs

    if old_to_new_refs:
        for dependency in cdx.get("dependencies", []):
            dependency["ref"] = old_to_new_refs.get(dependency.get("ref"), dependency.get("ref"))
            if "dependsOn" in dependency:
                dependency["dependsOn"] = [old_to_new_refs.get(ref, ref) for ref in dependency["dependsOn"]]
    save(root / "SBOM.cdx.json", cdx)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
