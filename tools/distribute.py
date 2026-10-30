#!/usr/bin/env python3
"""Build and publish the full-position Meshtastic distribution."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile

CONTROL = Path(__file__).resolve().parents[1]
UPSTREAM = "meshtastic/firmware"
CUTOFF = "2026-10-29T00:00:00Z"


def run(*args, cwd=None, capture=False, env=None):
    result = subprocess.run(args, cwd=cwd, env=env, check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def api(path):
    return json.loads(run("gh", "api", path, capture=True))


def policy_id():
    data = b"".join((CONTROL / "policy" / name).read_bytes()
                    for name in ("position.patch", "test_main.cpp"))
    data += Path(__file__).read_bytes()
    return hashlib.sha256(data).hexdigest()[:10]


def candidates(releases, develop):
    published = [r for r in releases if not r["draft"]]
    selected = {r["tag_name"]: r for r in published if r["published_at"] >= CUTOFF}
    for choices in (published, [r for r in published if not r["prerelease"]]):
        if choices:
            selected[choices[0]["tag_name"]] = choices[0]
    result = [{"track": "release", "upstream_ref": r["tag_name"],
               "prerelease": r["prerelease"]} for r in selected.values()]
    result.append({"track": "develop", "upstream_ref": develop, "prerelease": True})
    return result


def scan():
    repo = os.environ["GITHUB_REPOSITORY"]
    releases = api(f"repos/{UPSTREAM}/releases?per_page=100")
    develop = api(f"repos/{UPSTREAM}/commits/develop")["sha"]
    runs = api(f"repos/{repo}/actions/workflows/build.yml/runs?per_page=100")["workflow_runs"]
    active = {r["display_title"] for r in runs if r["status"] != "completed"}
    existing = api(f"repos/{repo}/releases?per_page=100")
    complete = {r["tag_name"] for r in existing if not r["draft"]}
    for item in candidates(releases, develop):
        source = api(f"repos/{UPSTREAM}/commits/{item['upstream_ref']}")["sha"]
        label = item["upstream_ref"] if item["track"] == "release" else source[:12]
        tag = f"{item['track']}-{label}-p{policy_id()}"
        if not re.fullmatch(r"[A-Za-z0-9._-]+", tag):
            raise ValueError(f"Unsupported upstream tag: {label}")
        if tag in complete or tag in active:
            continue
        run("gh", "workflow", "run", "build.yml", "--repo", repo,
            "--ref", "distribution", "-f", f"source={source}", "-f", f"tag={tag}",
            "-f", f"track={item['track']}", "-f", f"upstream_ref={item['upstream_ref']}",
            "-f", f"prerelease={str(item['prerelease']).lower()}")


def customize(root):
    cpp = root / "src/mesh/PositionPrecision.cpp"
    text = cpp.read_text()
    if "MAX_POSITION_PRECISION_PUBLIC_KEY" in text:
        run("git", "apply", "--check", str(CONTROL / "policy/position.patch"), cwd=root)
        run("git", "apply", str(CONTROL / "policy/position.patch"), cwd=root)
        test = root / "test/test_position_precision/test_main.cpp"
        if test.exists():
            test.write_text(test.read_text().replace("MAX_POSITION_PRECISION_PUBLIC_KEY", "32")
                            .replace("clampsPreciseOnDefaultKeyChannel", "keepsPreciseOnDefaultKeyChannel"))
    elif "return getPositionPrecisionForChannel(channels.getByIndex(channelIndex));" not in text:
        raise ValueError("Unknown upstream precision policy; review the customization before publishing")
    regression = root / "test/test_fork_position_policy/test_main.cpp"
    regression.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(CONTROL / "policy/test_main.cpp", regression)
    fix_legacy_rp_package_names(root)


def fix_legacy_rp_package_names(root):
    for platform in ("rp2040", "rp2350"):
        path = root / f"variants/{platform}/{platform}.ini"
        if path.exists():
            text = path.read_text()
            # The legacy alias installs an unused package; pin the framework PlatformIO actually builds.
            corrected = text.replace(
                "  arduino-pico@https://github.com/earlephilhower/arduino-pico/releases/download/",
                "  framework-arduinopico@https://github.com/earlephilhower/arduino-pico/releases/download/")
            if corrected != text:
                path.write_text(corrected)


def prepare():
    root = Path("firmware").resolve()
    source, tag = os.environ["SOURCE"], os.environ["TAG"]
    if not re.fullmatch(r"[0-9a-f]{40}", source) or not re.fullmatch(r"[A-Za-z0-9._-]+", tag):
        raise ValueError("Invalid immutable source or release tag")
    if run("git", "rev-parse", "HEAD", cwd=root, capture=True) != source:
        raise ValueError("Source checkout does not match the requested upstream commit")
    customize(root)
    # Publishing automation lives on distribution; firmware snapshots do not carry upstream workflows.
    shutil.rmtree(root / ".github/workflows")
    metadata = {"upstream": UPSTREAM, "upstream_commit": source,
                "upstream_ref": os.environ["UPSTREAM_REF"], "track": os.environ["TRACK"],
                "policy": policy_id(), "tag": tag}
    (root / "full-position-build.json").write_text(json.dumps(metadata, indent=2) + "\n")
    run("git", "add", "src/mesh/PositionPrecision.cpp", "src/mesh/PositionPrecision.h",
        "src/modules/AdminModule.cpp", "test", "variants", ".github/workflows", "full-position-build.json", cwd=root)
    date = run("git", "show", "-s", "--format=%cI", "HEAD", cwd=root, capture=True)
    env = dict(os.environ, GIT_AUTHOR_DATE=date, GIT_COMMITTER_DATE=date)
    run("git", "-c", "user.name=Full Position Builds", "-c", "user.email=full-position@users.noreply.github.com",
        "commit", "-m", "Allow configured position precision on public channels", cwd=root, env=env)
    sha = run("git", "rev-parse", "HEAD", cwd=root, capture=True)
    repo = os.environ["GITHUB_REPOSITORY"]
    run("git", "remote", "add", "distribution", f"https://github.com/{repo}.git", cwd=root)
    run("gh", "auth", "setup-git")
    run("git", "push", "distribution", f"HEAD:refs/heads/builds/{tag}", cwd=root)
    matrix = json.loads(run(sys.executable, "bin/generate_ci_matrix.py", "all", "--level", "extra",
                            cwd=root, capture=True))
    supported = {board["board"] for board in json.loads(run(
        sys.executable, "bin/generate_ci_matrix.py", "all", cwd=root, capture=True))}
    for board in matrix:
        board["required"] = board["board"] in supported
    groups = group_boards(matrix)
    (CONTROL / "matrix.json").write_text(json.dumps(matrix))
    with open(os.environ["GITHUB_OUTPUT"], "a") as output:
        output.write(f"source_commit={sha}\nmatrix={json.dumps({'include': groups}, separators=(',', ':'))}\n")


def group_boards(matrix):
    platforms = {}
    for board in matrix:
        if board["platform"] != "native":
            platforms.setdefault((board["platform"], board.get("required", True)), []).append(board["board"])
    groups = []
    for (platform, required), boards in sorted(platforms.items()):
        for start in range(0, len(boards), 4):
            groups.append({"platform": platform, "boards": boards[start:start + 4], "required": required,
                           "id": f"{platform}-{'supported' if required else 'extra'}-{start // 4:03d}"})
    if not groups or len(groups) > 256:
        raise ValueError("Invalid board matrix")
    return groups


def build():
    root = Path("firmware").resolve()
    platform, tag = os.environ["PLATFORM"], os.environ["TAG"]
    image = f"ghcr.io/meshtastic/gh-action-firmware:develop-{platform}"
    run("docker", "pull", image)
    digest = run("docker", "image", "inspect", image, "--format", "{{index .RepoDigests 0}}", capture=True)
    output = Path("downloads")
    output.mkdir(exist_ok=True)
    entries = []
    for board in json.loads(os.environ["BOARDS"]):
        if not re.fullmatch(r"[A-Za-z0-9_-]+", board):
            raise ValueError("Invalid board name")
        try:
            run("docker", "run", "--rm", "-e", "GITHUB_ACTIONS=true", "-e", "MT_TARGET=build",
                "-e", f"MT_PLATFORM={platform}", "-e", f"MT_ENV={board}",
                "-e", "PLATFORMIO_BUILD_DIR=/workspace/.pio/build", "-v", f"{root}:/workspace", digest)
        except subprocess.CalledProcessError:
            if os.environ.get("REQUIRED", "true") == "true":
                raise
            print(f"::warning::Experimental board {board} did not build; no firmware will be published for it", flush=True)
            continue
        release = root / "release"
        ota = None
        if platform in ("esp32", "esp32s3"):
            ota = (f"mt-{platform}-ota.bin", f"https://github.com/meshtastic/esp32-unified-ota/releases/latest/download/mt-{platform}-ota.bin")
        elif platform in ("esp32c3", "esp32c6"):
            ota = ("bleota-c3.bin", "https://github.com/meshtastic/firmware-ota/releases/latest/download/firmware-c3.bin")
        if ota:
            urllib.request.urlretrieve(ota[1], release / ota[0])
        files = sorted(p for p in release.iterdir() if p.is_file() and p.suffix not in (".elf", ".map", ".ld"))
        if not any(p.suffix in (".bin", ".uf2", ".hex", ".zip") and p.name.startswith("firmware-") for p in files):
            raise ValueError(f"No flashable firmware for {board}")
        filename = f"firmware-{board}-{tag}.zip"
        with zipfile.ZipFile(output / filename, "w", zipfile.ZIP_DEFLATED) as archive:
            for path in files:
                archive.write(path, path.name)
            archive.write(root / "full-position-build.json", "full-position-build.json")
        entries.append({"board": board, "platform": platform, "file": filename,
                        "sha256": hashlib.sha256((output / filename).read_bytes()).hexdigest(),
                        "builder_image": digest})
    (output / f"batch-{os.environ['BATCH']}.json").write_text(json.dumps(entries, indent=2))


def validate_outputs(matrix, entries):
    radio = {b["board"]: b for b in matrix if b["platform"] != "native"}
    required = {name for name, board in radio.items() if board.get("required", True)}
    actual = {b["board"] for b in entries}
    if not required.issubset(actual) or not actual.issubset(radio) or len(actual) != len(entries):
        raise ValueError("Incomplete or duplicate supported-board outputs; release will remain unpublished")
    return sorted(set(radio) - actual)


def publish():
    root = Path("downloads")
    entries = [entry for path in sorted(root.glob("batch-*.json")) for entry in json.loads(path.read_text())]
    unavailable = validate_outputs(json.loads(Path("matrix.json").read_text()), entries)
    for entry in entries:
        actual = hashlib.sha256((root / entry["file"]).read_bytes()).hexdigest()
        if actual != entry["sha256"]:
            raise ValueError(f"Checksum mismatch: {entry['board']}")
    (root / "manifest.json").write_text(json.dumps({"boards": entries, "unavailable_experimental_boards": unavailable}, indent=2) + "\n")
    (root / "SHA256SUMS").write_text("".join(f"{b['sha256']}  {b['file']}\n" for b in entries))
    repo, tag, sha = os.environ["GITHUB_REPOSITORY"], os.environ["TAG"], os.environ["SOURCE_COMMIT"]
    prerelease = os.environ["PRERELEASE"] == "true"
    notes = root / "release-notes.md"
    notes.write_text(f"Custom Meshtastic firmware based on [{os.environ['UPSTREAM_REF']}](https://github.com/{UPSTREAM}/commit/{os.environ['SOURCE']}).\n\n"
                     f"Public and unencrypted channels honor configured position precision, including full precision (32). Position sharing remains opt-in; precision 0 disables it.\n\n"
                     f"Includes {len(entries)} radio board variants. Download the ZIP matching your exact board. Checksums are in SHA256SUMS; complete source is at commit `{sha}`.\n\n"
                     f"Radio limits and other firmware behavior follow upstream. This is an independent fork.\n")
    if unavailable:
        with notes.open("a") as output:
            output.write("\nExperimental variants without a successful build (no download provided): "
                         + ", ".join(f"`{board}`" for board in unavailable) + ".\n")
    view = subprocess.run(["gh", "release", "view", tag, "--repo", repo, "--json", "isDraft"],
                          capture_output=True, text=True)
    if view.returncode:
        args = ["gh", "release", "create", tag, "--repo", repo, "--target", sha, "--draft",
                "--title", f"Full Position - {os.environ['UPSTREAM_REF']}", "--notes-file", str(notes)]
        if prerelease:
            args.append("--prerelease")
        run(*args)
    elif not json.loads(view.stdout)["isDraft"]:
        return
    assets = [str(root / b["file"]) for b in entries] + [str(root / "manifest.json"), str(root / "SHA256SUMS")]
    for start in range(0, len(assets), 20):
        run("gh", "release", "upload", tag, "--repo", repo, "--clobber", *assets[start:start + 20])
    run("gh", "release", "edit", tag, "--repo", repo, "--draft=false",
        f"--prerelease={str(prerelease).lower()}", f"--latest={str(not prerelease).lower()}")
    run("gh", "workflow", "run", "page.yml", "--repo", repo, "--ref", "distribution")


if __name__ == "__main__":
    {"scan": scan, "prepare": prepare, "build": build, "publish": publish}[sys.argv[1]]()
