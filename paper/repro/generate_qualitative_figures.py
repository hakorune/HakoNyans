#!/usr/bin/env python3
import subprocess
from pathlib import Path


SAMPLES = [
    {
        "key": "anime",
        "src": "test_images/anime/anime_sunset.ppm",
        "crop": "760x760+560+140",
    },
    {
        "key": "ui",
        "src": "test_images/ui/vscode.ppm",
        "crop": "980x500+420+220",
    },
    {
        "key": "photo",
        "src": "test_images/photo/nature_01.ppm",
        "crop": "960x560+500+260",
    },
]


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"Failed: {' '.join(cmd)}\nSTDOUT:\n{p.stdout}\nSTDERR:\n{p.stderr}")


def label_image(inp: Path, out: Path, label: str):
    run(
        [
            "convert",
            str(inp),
            "-gravity",
            "south",
            "-background",
            "#111111",
            "-fill",
            "white",
            "-splice",
            "0x54",
            "-pointsize",
            "26",
            "-annotate",
            "+0+10",
            label,
            str(out),
        ]
    )


def build():
    repo = Path(__file__).resolve().parents[2]
    hkn = repo / "build/hakonyans"
    out_root = repo / "paper/results/fig_artifacts"
    fig_root = repo / "paper/figures"
    out_root.mkdir(parents=True, exist_ok=True)
    fig_root.mkdir(parents=True, exist_ok=True)

    for s in SAMPLES:
        key = s["key"]
        src = repo / s["src"]
        base = out_root / key
        base.mkdir(parents=True, exist_ok=True)

        orig_png = base / "orig.png"
        jpg = base / "jpeg_q75.jpg"
        jpg_ppm = base / "jpeg_q75_out.ppm"
        jpg_png = base / "jpeg_q75_out.png"
        hkn_file = base / "hkn_q75_444_cfl1.hkn"
        hkn_ppm = base / "hkn_q75_444_cfl1_out.ppm"
        hkn_png = base / "hkn_q75_444_cfl1_out.png"

        run(["convert", str(src), str(orig_png)])
        run(["cjpeg", "-quality", "75", "-optimize", "-outfile", str(jpg), str(src)])
        run(["djpeg", "-outfile", str(jpg_ppm), str(jpg)])
        run(["convert", str(jpg_ppm), str(jpg_png)])

        run(
            [
                str(hkn),
                "encode",
                str(src),
                str(hkn_file),
                "75",
                "0",  # 444
                "1",  # CfL on
                "0",
            ]
        )
        run([str(hkn), "decode", str(hkn_file), str(hkn_ppm)])
        run(["convert", str(hkn_ppm), str(hkn_png)])

        # full strips
        l_orig = base / "orig_labeled.png"
        l_jpg = base / "jpeg_labeled.png"
        l_hkn = base / "hkn_labeled.png"
        label_image(orig_png, l_orig, "Original")
        label_image(jpg_png, l_jpg, "JPEG Q75")
        label_image(hkn_png, l_hkn, "HKN Q75 4:4:4 CfL=1")

        strip = fig_root / f"qual_{key}_strip.png"
        run(["montage", str(l_orig), str(l_jpg), str(l_hkn), "-tile", "3x1", "-geometry", "+8+8", str(strip)])

        # crop strips
        crop = s["crop"]
        c_orig = base / "orig_crop.png"
        c_jpg = base / "jpeg_crop.png"
        c_hkn = base / "hkn_crop.png"
        run(["convert", str(orig_png), "-crop", crop, "+repage", str(c_orig)])
        run(["convert", str(jpg_png), "-crop", crop, "+repage", str(c_jpg)])
        run(["convert", str(hkn_png), "-crop", crop, "+repage", str(c_hkn)])

        lc_orig = base / "orig_crop_labeled.png"
        lc_jpg = base / "jpeg_crop_labeled.png"
        lc_hkn = base / "hkn_crop_labeled.png"
        label_image(c_orig, lc_orig, "Original (Crop)")
        label_image(c_jpg, lc_jpg, "JPEG Q75 (Crop)")
        label_image(c_hkn, lc_hkn, "HKN Q75 (Crop)")

        crop_strip = fig_root / f"qual_{key}_crop_strip.png"
        run(["montage", str(lc_orig), str(lc_jpg), str(lc_hkn), "-tile", "3x1", "-geometry", "+8+8", str(crop_strip)])

        print(f"[ok] generated figure set: {key}")

    print(f"Figures written to: {fig_root}")


if __name__ == "__main__":
    build()
