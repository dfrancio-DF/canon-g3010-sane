# Canon CHMP SANE backend

An experimental SANE backend that exposes Canon CHMP network scanners to
Simple Scan, `scanimage`, and other SANE frontends. It wraps the CHMP network
client for Canon PIXMA G3010-series devices.

The CHMP protocol client is derived from
[`wailun42/canon-g3010-chmp-scanner`](https://github.com/wailun42/canon-g3010-chmp-scanner).
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attribution and license.

## Status and scope

- Tested against a Canon PIXMA G3111, which advertises itself as G3010 series.
- Flatbed, single-page A4 scans.
- Gray or color at 150, 300, or 600 dpi.
- The backend decodes the CHMP JPEG response into SANE Gray/RGB frames.
- No automatic network discovery, feeder, USB, or mid-scan cancellation.
- Not an official Canon driver; the CHMP protocol is reverse-engineered.

## Build and install

On Ubuntu 26.04 amd64:

```sh
sudo apt install build-essential libsane-dev libjpeg-dev python3
make
./build-deb.sh
sudo apt install ./dist/sane-backend-canon-chmp_0.1.4_amd64.deb
```

Configure the printer address in `/etc/sane.d/canon-chmp.conf`, one IPv4
address per line, then restart the scan application:

```text
192.168.1.50
```

Verify device discovery with:

```sh
scanimage -L
```

## Fast JPEG-to-PDF from the CHMP command line

`img2pdf` embeds the generated JPEG without decoding and re-encoding it:

```sh
sudo apt install img2pdf
base="$HOME/Documents/scan-$(date +%Y%m%d-%H%M%S)"
python3 /usr/lib/canon-chmp/chmp_scan.py \
  --host 192.168.1.50 --dpi 300 --gray --out "$base.jpg" &&
img2pdf --dpi 300 "$base.jpg" -o "$base.pdf"
```

Replace `192.168.1.50` with the printer's current IP. Omit `--gray` for color.
