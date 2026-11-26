#!/bin/bash
# AmenOS 3.0 - budowa obrazu ISO na bazie Lubuntu 26.04 LTS "Resolute Raccoon"
#
#   sudo ./dystrybucja/zbuduj.sh            # cała budowa (ok. 3 GB pobierania, 15 GB miejsca)
#   sudo ./dystrybucja/zbuduj.sh iso        # tylko złożenie ISO z gotowego chroota
#   sudo ./dystrybucja/zbuduj.sh konfiguruj iso   # ponowne nałożenie aplikacji i wyglądu + ISO
#
# Etapy: baza (debootstrap) → pakiety → konfiguruj (aplikacje, wygląd, język) → iso
# Wymagane na komputerze budującym (Ubuntu/Debian):
#   apt install debootstrap squashfs-tools xorriso dosfstools mtools
set -euo pipefail

WERSJA="3.0"
WYDANIE="resolute"
MIRROR="${MIRROR:-http://archive.ubuntu.com/ubuntu}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
D="$REPO/dystrybucja"
B="${BUDOWA:-$D/wyniki/budowa}"
C="$B/chroot"
ISO_DIR="$B/iso"
WYNIK="${WYNIK:-$D/wyniki/amenos-$WERSJA-amd64.iso}"

log() { printf '\n\033[1;33m✝ %s\033[0m\n' "$*"; }
[ "$(id -u)" = 0 ] || { echo "Uruchom jako root (sudo)."; exit 1; }
for n in debootstrap mksquashfs xorriso mkfs.vfat mcopy; do
  command -v $n >/dev/null || { echo "Brak programu: $n (apt install debootstrap squashfs-tools xorriso dosfstools mtools)"; exit 1; }
done

zamontuj() {
  for m in proc sys dev dev/pts run; do mountpoint -q "$C/$m" || mount --bind /$m "$C/$m"; done
}
odmontuj() {
  for m in run dev/pts dev sys proc; do mountpoint -q "$C/$m" && umount -l "$C/$m" || true; done
}
trap odmontuj EXIT
w_chroot() { chroot "$C" /usr/bin/env -i HOME=/root PATH=/usr/sbin:/usr/bin:/sbin:/bin LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive "$@"; }

# ---------------------------------------------------------------- 1. baza
etap_baza() {
  [ -f "$C/.amenos-baza" ] && { log "Baza już jest - pomijam"; return; }
  log "debootstrap $WYDANIE"
  mkdir -p "$B"
  debootstrap --arch=amd64 --components=main,restricted,universe,multiverse "$WYDANIE" "$C" "$MIRROR"
  touch "$C/.amenos-baza"
}

# ---------------------------------------------------------------- 2. pakiety
etap_pakiety() {
  log "Źródła apt i pakiety Lubuntu"
  cat > "$C/etc/apt/sources.list" <<EOF
deb $MIRROR $WYDANIE main restricted universe multiverse
deb $MIRROR $WYDANIE-updates main restricted universe multiverse
deb $MIRROR $WYDANIE-security main restricted universe multiverse
EOF
  rm -f "$C/etc/apt/sources.list.d/ubuntu.sources"
  # bez snapów (live ISO bez sieci i tak by ich nie zainstalowało); przeglądarka: Falkon
  cat > "$C/etc/apt/preferences.d/amenos-bez-snapow" <<'EOF'
Package: snapd firefox thunderbird snapd-installation-monitor fwupd-snap
Pin: release *
Pin-Priority: -1
EOF
  printf '#!/bin/sh\nexit 101\n' > "$C/usr/sbin/policy-rc.d"; chmod +x "$C/usr/sbin/policy-rc.d"
  echo amenos > "$C/etc/hostname"
  zamontuj
  w_chroot apt-get update
  w_chroot apt-get -y -o Dpkg::Options::=--force-confnew dist-upgrade
  w_chroot apt-get install -y -o Dpkg::Options::=--force-confnew $(grep -v '^#' "$D/pakiety.txt" | xargs)
  touch "$C/.amenos-pakiety"
}

# ---------------------------------------------------------------- 3. konfiguracja
zbuduj_deb() {
  local p="$B/deb/amenos-parafia"
  rm -rf "$B/deb"; mkdir -p "$p/DEBIAN" "$p/usr/bin" "$p/usr/share/amenos" "$p/usr/share/applications" \
     "$p/usr/share/desktop-directories" "$p/etc/xdg/menus/applications-merged" "$p/etc/xdg/autostart"
  sed "s/@WERSJA@/$WERSJA/" "$D/pakiet/control" > "$p/DEBIAN/control"
  cp -r "$REPO/aplikacje" "$p/usr/share/amenos/aplikacje"
  rm -rf "$p/usr/share/amenos/aplikacje/narzedzia" "$p/usr/share/amenos/aplikacje/amenos-app"
  find "$p/usr/share/amenos" -name __pycache__ -prune -exec rm -rf {} +
  install -m755 "$REPO/aplikacje/amenos-app" "$p/usr/bin/amenos-app"
  cp "$D"/pakiet/desktop/*.desktop "$p/usr/share/applications/"
  cp "$D/pakiet/amenos-parafia.directory" "$p/usr/share/desktop-directories/"
  cp "$D/pakiet/amenos.menu" "$p/etc/xdg/menus/applications-merged/"
  cp "$D/pakiet/autostart-amenos.desktop" "$p/etc/xdg/autostart/amenos-dzis.desktop"
  for r in 256 128 64 48 32; do
    mkdir -p "$p/usr/share/icons/hicolor/${r}x${r}/apps"
    for f in "$REPO"/aplikacje/wspolne/ikony/*.png; do
      n=$(basename "$f" .png); [ "$n" = amenos ] && cel=amenos || cel="amenos-$n"
      python3 - "$f" "$p/usr/share/icons/hicolor/${r}x${r}/apps/$cel.png" "$r" <<'EOF' 2>/dev/null || cp "$f" "$p/usr/share/icons/hicolor/${r}x${r}/apps/$cel.png"
import sys
from PIL import Image
Image.open(sys.argv[1]).resize((int(sys.argv[3]),) * 2, Image.LANCZOS).save(sys.argv[2])
EOF
    done
  done
  dpkg-deb --root-owner-group -b "$p" "$B/deb/amenos-parafia_${WERSJA}_all.deb" >/dev/null
}

ustaw_klucz() {  # plik sekcja klucz wartość - zmienia jedną linię, resztę pliku zostawia w spokoju
  python3 - "$@" <<'EOF'
import sys
p, s, k, v = sys.argv[1:5]
linie = open(p, encoding="utf-8").read().splitlines()
start = next((i for i, l in enumerate(linie) if l.strip() == f"[{s}]"), None)
if start is None:
    linie += ["", f"[{s}]", f"{k}={v}"]
else:
    koniec = next((i for i in range(start + 1, len(linie)) if linie[i].startswith("[")), len(linie))
    for i in range(start + 1, koniec):
        if linie[i].split("=", 1)[0].strip() == k:
            linie[i] = f"{k}={v}"
            break
    else:
        linie.insert(start + 1, f"{k}={v}")
open(p, "w", encoding="utf-8").write("\n".join(linie) + "\n")
EOF
}

etap_konfiguruj() {
  zamontuj
  printf '#!/bin/sh\nexit 101\n' > "$C/usr/sbin/policy-rc.d"; chmod +x "$C/usr/sbin/policy-rc.d"
  log "Pakiet amenos-parafia"
  zbuduj_deb
  cp "$B/deb/amenos-parafia_${WERSJA}_all.deb" "$C/tmp/"
  w_chroot apt-get install -y --reinstall "/tmp/amenos-parafia_${WERSJA}_all.deb"
  rm -f "$C"/tmp/amenos-parafia_*.deb

  log "Nazwa systemu"
  sed -i -e "s/^PRETTY_NAME=.*/PRETTY_NAME=\"AmenOS $WERSJA (Lubuntu 26.04 LTS)\"/" \
         -e "s/^NAME=.*/NAME=\"AmenOS\"/" -e "s|^HOME_URL=.*|HOME_URL=\"https://github.com/karoljasianek/AmenOS\"|" "$C/usr/lib/os-release"
  grep -q '^VARIANT=' "$C/usr/lib/os-release" || echo "VARIANT=\"system dla parafii\"" >> "$C/usr/lib/os-release"
  sed -i "s/^DISTRIB_DESCRIPTION=.*/DISTRIB_DESCRIPTION=\"AmenOS $WERSJA\"/" "$C/etc/lsb-release"
  echo "AmenOS $WERSJA \\n \\l" > "$C/etc/issue"
  cat > "$C/etc/casper.conf" <<'EOF'
export USERNAME="parafia"
export USERFULLNAME="Parafia (sesja na żywo)"
export HOST="amenos"
export BUILD_SYSTEM="Ubuntu"
export FLAVOUR="AmenOS"
EOF

  log "Język polski, klawiatura, strefa czasowa"
  sed -i 's/^# *pl_PL.UTF-8/pl_PL.UTF-8/' "$C/etc/locale.gen" 2>/dev/null || true
  w_chroot locale-gen pl_PL.UTF-8 en_US.UTF-8
  printf 'LANG=pl_PL.UTF-8\nLANGUAGE=pl:en\n' > "$C/etc/default/locale"
  cat > "$C/etc/default/keyboard" <<'EOF'
XKBMODEL="pc105"
XKBLAYOUT="pl"
XKBVARIANT=""
XKBOPTIONS=""
BACKSPACE="guess"
EOF
  ln -sf /usr/share/zoneinfo/Europe/Warsaw "$C/etc/localtime"; echo Europe/Warsaw > "$C/etc/timezone"

  log "Tapeta, ekran logowania, panel"
  mkdir -p "$C/usr/share/amenos/wyglad"
  cp "$D/wyglad/tapeta.jpg" "$D/wyglad/tapeta-1920.jpg" "$C/usr/share/amenos/wyglad/"
  T=/usr/share/amenos/wyglad/tapeta.jpg
  # pcmanfm-qt: wszystkie systemowe ustawienia pulpitu (Lubuntu trzyma je w xdg-Lubuntu)
  find "$C/etc/xdg" "$C/usr/share" -path '*pcmanfm-qt/lxqt/settings.conf' 2>/dev/null | while read -r f; do
    ustaw_klucz "$f" Desktop Wallpaper "$T"; ustaw_klucz "$f" Desktop WallpaperMode zoom
    ustaw_klucz "$f" Desktop DesktopShortcuts "Home, Trash, Computer, Network"
  done
  # SDDM: tło motywu Lubuntu (theme.conf.user ma pierwszeństwo przed theme.conf)
  for t in "$C"/usr/share/sddm/themes/*/; do
    [ -f "$t/theme.conf" ] && printf '[General]\nbackground=%s\n' "$T" > "$t/theme.conf.user"
  done
  # LXQt: szybkie uruchamianie programów parafii na panelu
  find "$C/etc/xdg" -path '*lxqt/panel.conf' 2>/dev/null | while read -r f; do
    grep -q '^\[fancymenu\]' "$f" && { ustaw_klucz "$f" fancymenu ownIcon true; ustaw_klucz "$f" fancymenu icon /usr/share/icons/hicolor/256x256/apps/amenos.png; }
    python3 - "$f" <<'EOF'
import sys, re
p = sys.argv[1]; s = open(p, encoding="utf-8").read()
if "amenos-dzis.desktop" in s: sys.exit()
m = re.search(r"^\[quicklaunch\]\n((?:(?!\[).*\n?)*)", s, re.M)
if not m: sys.exit()
blok = m.group(1)
n = len(re.findall(r"^apps\\\d+\\desktop=", blok, re.M))
nowe = "".join(f"apps\\{n+i+1}\\desktop=/usr/share/applications/amenos-{a}.desktop\n" for i, a in enumerate(["dzis", "piesni", "kalendarz", "modlitewnik"]))
blok2 = re.sub(r"^apps\\size=\d+", lambda _: f"apps\\size={n+4}", blok, flags=re.M)
if "apps\\size=" not in blok2: blok2 = f"apps\\size={n+4}\n" + blok2
s = s[:m.start(1)] + blok2.rstrip("\n") + "\n" + nowe + "\n" + s[m.end(1):]
open(p, "w", encoding="utf-8").write(s)
EOF
  done

  log "Plymouth"
  P="$C/usr/share/plymouth/themes/amenos"; mkdir -p "$P"
  cp "$D"/plymouth/amenos.{plymouth,script} "$D"/wyglad/plymouth/*.png "$P/"
  w_chroot update-alternatives --install /usr/share/plymouth/themes/default.plymouth default.plymouth /usr/share/plymouth/themes/amenos/amenos.plymouth 200
  w_chroot update-alternatives --set default.plymouth /usr/share/plymouth/themes/amenos/amenos.plymouth

  log "Ekran powitalny sesji na żywo"
  if [ -e "$C/usr/bin/lubuntu-installer-prompt" ] && ! chroot "$C" dpkg-divert --list /usr/bin/lubuntu-installer-prompt | grep -q .; then
    chroot "$C" dpkg-divert --local --rename --divert /usr/bin/lubuntu-installer-prompt.lubuntu --add /usr/bin/lubuntu-installer-prompt >/dev/null
  fi
  [ -e "$C/usr/bin/lubuntu-installer-prompt.lubuntu" ] && install -m755 "$D/konfiguracja/amenos-powitanie" "$C/usr/bin/lubuntu-installer-prompt"

  log "Instalator Calamares"
  if [ -d "$C/etc/calamares" ]; then
    K="$C/etc/calamares/branding/amenos"; rm -rf "$K"; mkdir -p "$K"
    stary=$(ls -d "$C"/etc/calamares/branding/*/ | grep -v /amenos/ | head -1)
    [ -n "$stary" ] && cp -r "$stary"/. "$K/"
    cp "$D"/wyglad/calamares/*.png "$K/"
    sed "s/@WERSJA@/$WERSJA/g" "$D/calamares/branding.desc" > "$K/branding.desc"
    sed -i 's/^branding:.*/branding: amenos/' "$C/etc/calamares/settings.conf"
    W="$C/etc/calamares/modules/welcome.conf"
    [ -f "$W" ] && sed -i -e 's|^showDonateUrl:.*|showDonateUrl: false|' -e 's|^\( *internetCheckUrl:\).*|\1 http://connectivity-check.ubuntu.com|' "$W"
    [ -f "$C/etc/calamares/modules/locale.conf" ] && sed -i -e 's/^\( *region:\).*/\1 "Europe"/' -e 's/^\( *zone:\).*/\1 "Warsaw"/' "$C/etc/calamares/modules/locale.conf"
    for f in "$C"/usr/share/applications/*calamares*.desktop "$C"/usr/share/applications/lubuntu-*install*.desktop; do
      [ -f "$f" ] && sed -i -e 's/^Name\(\[[a-zA-Z_]*\]\)\?=.*/Name\1=Zainstaluj AmenOS/' -e 's/^Icon=.*/Icon=amenos/' "$f"
    done
  fi

  log "initramfs (casper.conf, plymouth)"
  w_chroot update-initramfs -u -k all

  log "Sprzątanie"
  w_chroot apt-get -y autoremove --purge
  w_chroot apt-get clean
  rm -f "$C/usr/sbin/policy-rc.d" "$C"/var/lib/apt/lists/*_Packages "$C"/var/lib/apt/lists/*_Translation-* 2>/dev/null || true
  : > "$C/etc/machine-id"; rm -f "$C/var/lib/dbus/machine-id"
  rm -rf "$C/tmp/"* "$C/root/.cache" "$C/var/log/"*.log
}

# ---------------------------------------------------------------- 4. obraz ISO
etap_iso() {
  odmontuj
  log "Składam ISO"
  rm -rf "$ISO_DIR"; mkdir -p "$ISO_DIR"/{casper,boot/grub/fonts,.disk,EFI/boot}
  K=$(ls "$C"/boot/vmlinuz-* | sort -V | tail -1); W=${K##*/vmlinuz-}
  cp "$K" "$ISO_DIR/casper/vmlinuz"; cp "$C/boot/initrd.img-$W" "$ISO_DIR/casper/initrd"
  chroot "$C" dpkg-query -W --showformat='${Package} ${Version}\n' > "$ISO_DIR/casper/filesystem.manifest"
  # te pakiety instalator usuwa z zainstalowanego systemu
  grep -E '^(casper|calamares|calamares-settings-lubuntu|cifs-utils) ' "$ISO_DIR/casper/filesystem.manifest" | cut -d' ' -f1 > "$ISO_DIR/casper/filesystem.manifest-remove" || true
  du -sx --block-size=1 "$C" | cut -f1 > "$ISO_DIR/casper/filesystem.size"
  log "squashfs (zstd) - to potrwa"
  mksquashfs "$C" "$ISO_DIR/casper/filesystem.squashfs" -noappend -comp zstd -Xcompression-level "${ZSTD:-19}" -b 1M \
    -e "$C/.amenos-baza" -e "$C/.amenos-pakiety" -wildcards -e 'proc/*' -e 'sys/*' -e 'dev/*' -e 'run/*' -e 'tmp/*' -e 'boot/*.bak'
  echo "AmenOS $WERSJA \"Resolute Raccoon\" - Release amd64 ($(date +%Y%m%d))" > "$ISO_DIR/.disk/info"
  touch "$ISO_DIR/.disk/base_installable"; echo "full_cd/single" > "$ISO_DIR/.disk/cd_type"
  cp "$C/usr/share/grub/unicode.pf2" "$ISO_DIR/boot/grub/fonts/"
  cp "$D/wyglad/grub-tlo.png" "$ISO_DIR/boot/grub/tlo.png"
  sed "s/@WERSJA@/$WERSJA/g" "$D/konfiguracja/grub.cfg" > "$ISO_DIR/boot/grub/grub.cfg"
  cp "$D/konfiguracja/grub-loopback.cfg" "$ISO_DIR/boot/grub/loopback.cfg"

  # BIOS: grub i386-pc (El Torito + hybrydowy MBR do zapisu na pendrive)
  mkdir -p "$ISO_DIR/boot/grub/i386-pc"
  cp "$C"/usr/lib/grub/i386-pc/*.{mod,lst} "$ISO_DIR/boot/grub/i386-pc/"
  chroot "$C" grub-mkimage -O i386-pc -p /boot/grub -o /tmp/core.img \
    biosdisk iso9660 part_msdos part_gpt normal search search_fs_file configfile linux echo test gfxterm png all_video font loopback
  cat "$C/usr/lib/grub/i386-pc/cdboot.img" "$C/tmp/core.img" > "$ISO_DIR/boot/grub/i386-pc/eltorito.img"
  rm -f "$C/tmp/core.img"

  # UEFI z Secure Boot: shim (podpisany przez Microsoft) → grub (podpisany przez Canonical)
  SHIM="$C/usr/lib/shim/shimx64.efi.signed.latest"; [ -f "$SHIM" ] || SHIM="$C/usr/lib/shim/shimx64.efi.signed"
  GRUBEFI="$C/usr/lib/grub/x86_64-efi-signed/gcdx64.efi.signed"
  MM="$C/usr/lib/shim/mmx64.efi"
  cp "$SHIM" "$ISO_DIR/EFI/boot/bootx64.efi"; cp "$GRUBEFI" "$ISO_DIR/EFI/boot/grubx64.efi"; [ -f "$MM" ] && cp "$MM" "$ISO_DIR/EFI/boot/"
  EFIIMG="$B/efi.img"; rm -f "$EFIIMG"
  truncate -s 10M "$EFIIMG"; mkfs.vfat -n AMENOS_EFI "$EFIIMG" >/dev/null
  mmd -i "$EFIIMG" ::/EFI ::/EFI/boot
  mcopy -i "$EFIIMG" "$ISO_DIR"/EFI/boot/*.efi ::/EFI/boot/

  mkdir -p "$(dirname "$WYNIK")"
  xorriso -as mkisofs -r -V "AmenOS $WERSJA amd64" -J -joliet-long -l -iso-level 3 \
    --grub2-mbr "$C/usr/lib/grub/i386-pc/boot_hybrid.img" -partition_offset 16 --mbr-force-bootable \
    -append_partition 2 0xef "$EFIIMG" -appended_part_as_gpt \
    -c /boot.catalog -b /boot/grub/i386-pc/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table --grub2-boot-info \
    -eltorito-alt-boot -e '--interval:appended_partition_2:all::' -no-emul-boot \
    -o "$WYNIK" "$ISO_DIR"
  (cd "$(dirname "$WYNIK")" && sha256sum "$(basename "$WYNIK")" > "$(basename "$WYNIK").sha256")
  log "Gotowe: $WYNIK ($(du -h "$WYNIK" | cut -f1))"
}

ETAPY=("$@"); [ ${#ETAPY[@]} = 0 ] && ETAPY=(baza pakiety konfiguruj iso)
for e in "${ETAPY[@]}"; do
  case "$e" in
    baza) etap_baza ;; pakiety) etap_pakiety ;; konfiguruj) etap_konfiguruj ;; iso) etap_iso ;;
    *) echo "Nieznany etap: $e (baza|pakiety|konfiguruj|iso)"; exit 1 ;;
  esac
done
