#!/usr/bin/env bash
# ============================================================================
#  luatools-moon installer, x86_64 edition
# ============================================================================
#  Runs the stock luatools-moon installer (checks, dependencies, cleanup, Lumen,
#  LuaTools plugin, Game Mode, desktop coverage) with the pieces that cannot work
#  with the 64-bit Steam client (steamrt64) replaced:
#
#    * slsteam-moon : the x86_64 build (release asset *-x86_64.zip, or a local zip)
#                     instead of the i386 release;
#    * CloudRedirect: skipped, its hook is a 32-bit LD_PRELOAD library that a 64-bit
#                     client ignores; DisableCloud is forced to "yes" so Steam does
#                     not attempt a cloud sync nobody answers.
#
#  With the legacy 32-bit client (sentinel .steam-disable-steamrt64-client, or
#  STEAM_FORCE_CLIENT=ubuntu12_32|scout|ldlp), or when no 64-bit client is found, the
#  installer stops before changing anything and says why; SLSM_ALLOW_STOCK=1 delegates
#  to the stock installer instead (upstream 32-bit build).
#
#  Lumen and the LuaTools menu ship with swwayps/* repositories hard-coded for the
#  update check, the "Update All" button and the auto-fix offer. After Lumen is
#  installed, those few strings are repointed to this repository (repoint_lumen), so
#  an update installs this build instead of the 32-bit one. Both scripts are pointed at
#  the release assets (releases/latest/download), which only change after the CI passed. If Lumen's files no longer
#  contain the expected strings, the installer says so and exits non-zero.
#
#  The stock installer is sourced, not forked: it ends with
#  `[ -z "$SLSPLUGIN_LIB_ONLY" ] && main`, so its functions load without running and
#  we override only the two above. Upstream fixes keep flowing in.
#
#  Usage
#    ./install-x86_64.sh [--slsteam-zip FILE] [stock installer options]
#    SLSM_ZIP=FILE        same as --slsteam-zip
#    LUATOOLS_INSTALL_FILE=FILE   use a local copy of the stock install.sh
#    LUATOOLS_INSTALL_URL=URL     where to fetch it (default: swwayps/luatools-moon main)
#    LUATOOLS_INSTALL_SHA256=HEX  refuse a stock installer whose sha256 differs
#
#  Build a local zip:  make && scripts/package.sh --host
# ============================================================================

set -uo pipefail

UPSTREAM_URL="${LUATOOLS_INSTALL_URL:-https://raw.githubusercontent.com/swwayps/luatools-moon/main/install.sh}"
UPSTREAM_FILE="${LUATOOLS_INSTALL_FILE:-}"
UPSTREAM_SHA256="${LUATOOLS_INSTALL_SHA256:-}"
SLSM_ZIP="${SLSM_ZIP:-}"
# x86_64 release asset of slsteam-moon (scripts/package.sh names it this way).
SLSM_X64_REPO="${SLSM_X64_REPO:-ogb4n/slsteam-moon-x64}"
SLSM_X64_ASSET_GLOB='^slsteam-moon-linux-.*-x86_64\.zip$'

die() { echo "install-x86_64: $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Our own options, removed before the stock parser sees the arguments.
# ---------------------------------------------------------------------------
PASSTHRU=()
while [ "$#" -gt 0 ]; do
	case "$1" in
		--slsteam-zip)   [ "$#" -ge 2 ] || die "--slsteam-zip needs a file"; SLSM_ZIP="$2"; shift ;;
		--slsteam-zip=*) SLSM_ZIP="${1#*=}" ;;
		*)               PASSTHRU+=("$1") ;;
	esac
	shift
done

# ---------------------------------------------------------------------------
# Which Steam client will actually run? Same rule as steam.sh and setup.sh:
# steamrt64 is the default; the sentinel or STEAM_FORCE_CLIENT selects the 32-bit one.
# ---------------------------------------------------------------------------
slsm_steam_root() {
	local r
	for r in "$HOME/.steam/steam" "$HOME/.steam/debian-installation" "$HOME/.local/share/Steam"; do
		if [ -f "$r/steam.sh" ]; then readlink -f "$r"; return 0; fi
	done
	return 1
}

slsm_client_is_64bit() {
	local root force64=0
	case "${STEAM_FORCE_CLIENT:-}" in
		ubuntu12_32|scout|ldlp) return 1 ;;
		steamrt64|steamrt3c|steamrt) force64=1 ;;
	esac
	root="$(slsm_steam_root)" || return 1
	if [ "$force64" != 1 ] && [ -e "$root/.steam-disable-steamrt64-client" ]; then
		return 1
	fi
	[ -x "$root/steamrt64/steam" ] && [ -f "$root/steamrt64/steamclient.so" ]
}

# Why slsm_client_is_64bit said no, in one line (printed when the install stops).
slsm_32bit_reason() {
	local root
	case "${STEAM_FORCE_CLIENT:-}" in
		ubuntu12_32|scout|ldlp)
			echo "the environment variable STEAM_FORCE_CLIENT=${STEAM_FORCE_CLIENT} forces the 32-bit client"; return ;;
	esac
	root="$(slsm_steam_root)" || {
		echo "no Steam installation (steam.sh) was found in ~/.steam/steam, ~/.steam/debian-installation or ~/.local/share/Steam"; return; }
	if [ "${STEAM_FORCE_CLIENT:-}" != steamrt64 ] && [ -e "$root/.steam-disable-steamrt64-client" ]; then
		echo "the file $root/.steam-disable-steamrt64-client forces the 32-bit client"; return
	fi
	echo "$root/steamrt64/ is missing or incomplete: Steam has not downloaded its 64-bit client yet"
}

# ---------------------------------------------------------------------------
# Load the stock installer (functions only).
# ---------------------------------------------------------------------------
load_stock_installer() {
	local file
	if [ -n "$UPSTREAM_FILE" ]; then
		[ -f "$UPSTREAM_FILE" ] || die "LUATOOLS_INSTALL_FILE not found: $UPSTREAM_FILE"
		file="$UPSTREAM_FILE"
	else
		command -v curl >/dev/null 2>&1 || die "curl is required to fetch the stock installer"
		STOCK_TMP="$(mktemp -d)"; trap 'rm -rf "${STOCK_TMP:-}"' EXIT
		file="$STOCK_TMP/install.sh"
		curl --proto '=https' --proto-redir '=https' -fsSL --connect-timeout 15 --retry 3 \
			"$UPSTREAM_URL" -o "$file" || die "could not fetch $UPSTREAM_URL"
	fi
	if [ -n "$UPSTREAM_SHA256" ]; then
		[ "$(sha256sum "$file" | awk '{print $1}')" = "$UPSTREAM_SHA256" ] \
			|| die "stock installer sha256 does not match LUATOOLS_INSTALL_SHA256"
	else
		echo "→ stock installer sha256: $(sha256sum "$file" | awk '{print $1}')"
	fi
	# shellcheck disable=SC1090
	SLSPLUGIN_LIB_ONLY=1 . "$file"
	declare -F main >/dev/null && declare -F install_slsteam_moon >/dev/null \
		&& declare -F install_cloudredirect >/dev/null \
		|| die "the stock installer does not define main/install_slsteam_moon/install_cloudredirect"
}

# ---------------------------------------------------------------------------
# Lumen ships with the upstream repositories hard-coded. Repoint the three places
# that would otherwise replace this 64-bit build with the 32-bit one: the update
# comparison, the Update All installer URL and the auto-fix script URL.
# Literal replacement (no regex); a string already replaced counts as done.
# ---------------------------------------------------------------------------
repoint_lumen() {
	local dir="${1:-${LUMEN_DIR:-$HOME/.local/share/Lumen}}"
	# Release assets, not the main branch: they only change after the CI passed.
	local dl="https://github.com/$SLSM_X64_REPO/releases/latest/download"
	local raw="https://raw.githubusercontent.com/$SLSM_X64_REPO/main"
	local up_install="https://raw.githubusercontent.com/swwayps/luatools-moon/main/install.sh"
	local up_autofix="https://raw.githubusercontent.com/swwayps/slsteam-moon/slsteam-moon/autofix.sh"
	local -a files=() from=() to=() opt=()
	shopt -u patsub_replacement 2>/dev/null || true
	add() { files+=("$1"); from+=("$2"); to+=("$3"); opt+=("${4:-0}"); }

	# First, Lumens repointed by an earlier version of this installer still serve both
	# scripts from the main branch: migrate them. Optional, so a fresh Lumen is not an error;
	# done before the upstream pairs so those see their replacement already in place.
	add lua/about.lua        "$raw/install-x86_64.sh" "$dl/install-x86_64.sh" 1
	add lua/menu/02-i18n.js  "$raw/install-x86_64.sh" "$dl/install-x86_64.sh" 1
	add lua/slscheck.lua     "$raw/autofix.sh"        "$dl/autofix.sh"        1
	add lua/menu/02-i18n.js  "$raw/autofix.sh"        "$dl/autofix.sh"        1

	add lua/about.lua "repo = \"swwayps/slsteam-moon\"," "repo = \"$SLSM_X64_REPO\","
	add lua/about.lua 'mirror_key = "slsteam-moon",' 'mirror_key = "slsteam-moon-x64",'
	add lua/about.lua 'asset_pat = "^slsteam%-moon%-linux%-.*%-lumen%.zip$",' 'asset_pat = "^slsteam%-moon%-linux%-.*%-x86_64%.zip$",'
	add lua/about.lua "$up_install" "$dl/install-x86_64.sh"
	add lua/slscheck.lua "$up_autofix" "$dl/autofix.sh"
	add lua/menu/02-i18n.js "$up_install" "$dl/install-x86_64.sh"
	add lua/menu/02-i18n.js "$up_autofix" "$dl/autofix.sh"

	local i f c rc=0
	for i in "${!files[@]}"; do
		f="$dir/${files[$i]}"
		if [ ! -f "$f" ]; then
			log_warn "repoint_lumen: ${files[$i]} not found in $dir"; rc=1; continue
		fi
		c="$(cat -- "$f"; printf x)"; c="${c%x}"
		if [[ "$c" == *"${from[$i]}"* ]]; then
			printf '%s' "${c//"${from[$i]}"/${to[$i]}}" > "$f" || { rc=1; continue; }
		elif [ "${opt[$i]}" = 0 ] && [[ "$c" != *"${to[$i]}"* ]]; then
			log_warn "repoint_lumen: expected text not found in ${files[$i]}: ${from[$i]}"; rc=1
		fi
	done
	if [ "$rc" -ne 0 ]; then
		log_warn "$(L "Lumen's files changed: its update check and Update All may still point at the 32-bit build. Do not use Update All; report this at https://github.com/$SLSM_X64_REPO/issues" \
		             "Os arquivos do Lumen mudaram: a verificação de updates e o Update All podem apontar para o build de 32 bits. Não use o Update All; avise em https://github.com/$SLSM_X64_REPO/issues")"
		SLSM_REPOINT_FAILED=1
	fi
	return "$rc"
}

# ---------------------------------------------------------------------------
# Overrides (defined after the stock functions are loaded).
# ---------------------------------------------------------------------------
define_overrides() {
	eval "orig_$(declare -f install_slsteam_moon)"
	eval "orig_$(declare -f install_cloudredirect)"
	if declare -F install_lumen >/dev/null; then
		eval "orig_$(declare -f install_lumen)"
		install_lumen() {
			orig_install_lumen "$@" || return
			slsm_client_is_64bit || return 0
			log_info "$(L "Pointing Lumen's updates at $SLSM_X64_REPO" "Apontando as atualizações do Lumen para $SLSM_X64_REPO")"
			repoint_lumen || true
		}
	fi

	install_slsteam_moon() {
		slsm_client_is_64bit || { orig_install_slsteam_moon; return; }

		local tmp zip extract_root setup setup_path rc=0
		tmp="$(mktemp -d)"; trap 'rm -rf "${tmp:-}"' RETURN
		zip="$tmp/slsteam-moon-x86_64.zip"
		SLS_INFO='{}'

		if [ "${OPT_SLS_CHANNEL:-stable}" = beta ]; then
			log_warn "$(L "No beta channel for the x86_64 build; using the stable asset." \
			             "Não há canal beta para o build x86_64; usando o asset stable.")"
		fi

		if [ -n "$SLSM_ZIP" ]; then
			[ -f "$SLSM_ZIP" ] || fail "$(L "Local zip not found: $SLSM_ZIP" "Zip local não encontrado: $SLSM_ZIP")"
			log_info "$(L "Using the local slsteam-moon x86_64 zip: $SLSM_ZIP" \
			             "Usando o zip local x86_64 do slsteam-moon: $SLSM_ZIP")"
			cp -- "$SLSM_ZIP" "$zip" || fail "$(L "Could not copy the zip" "Não foi possível copiar o zip")"
			if [ -f "$SLSM_ZIP.sha256" ]; then
				verify_sha256 "$zip" "$(cat "$SLSM_ZIP.sha256")" \
					|| fail "$(L "The local zip does not match its .sha256" "O zip local não bate com o .sha256")"
				log_info "$(L "Verified slsteam-moon" "Verificado: slsteam-moon")"
			else
				log_warn "$(L "No .sha256 next to the local zip; continuing unverified." \
				             "Sem .sha256 ao lado do zip local; continuando sem verificação.")"
			fi
			SLS_INFO="$(jq -cn --arg id "$(sha256sum "$zip" | awk '{print substr($1,1,12)}')" \
				'{tag:"x86_64-local", channel:"local", id:$id}' 2>/dev/null || printf '{}')"
		else
			log_info "$(L "Resolving the latest slsteam-moon x86_64 release" \
			             "Buscando a última release x86_64 do slsteam-moon")"
			# The x86_64 releases live in this repository, not in the stock installer's i386 one.
			SLS_REPO="${SLSM_X64_REPO:-ogb4n/slsteam-moon-x64}"
			resolve_component_asset stable "$SLS_REPO" "" "$SLSM_X64_ASSET_GLOB" any "" || rc=$?
			if [ "$rc" -eq 2 ]; then fail "$(forge_unreachable_msg)"; fi
			if [ "$rc" -ne 0 ]; then
				fail "$(L "No x86_64 slsteam-moon release asset found. Build one (scripts/package.sh) and pass --slsteam-zip FILE." \
				          "Nenhum asset x86_64 do slsteam-moon encontrado. Gere um (scripts/package.sh) e passe --slsteam-zip ARQUIVO.")"
			fi
			log_info "$(L "Downloading slsteam-moon (x86_64)" "Baixando slsteam-moon (x86_64)")"
			rc=0
			download_resolved_asset "$zip" "slsteam-moon" || rc=$?
			[ "$rc" -eq 0 ] || fail "$(forge_unreachable_msg)"
			SLS_INFO="$DOWNLOADED_ASSET_INFO"
		fi

		log_info "$(L "Extracting" "Extraindo")"
		extract_zip "$zip" "$tmp/extracted" || fail "$(L "Extraction failed" "Falha na extração")"
		setup="$(find "$tmp/extracted" -maxdepth 2 -name setup.sh -type f | head -n1)"
		[ -n "$setup" ] || fail "$(L "setup.sh not found in the archive." "setup.sh não encontrado no pacote.")"
		extract_root="$(dirname "$setup")"

		# The archive must really carry the build for this client: setup.sh also checks,
		# but failing here keeps the stock cleanup from being the last thing that ran.
		if [ "$(od -An -tx1 -j4 -N1 "$extract_root/bin/SLSsteam.so" 2>/dev/null | tr -d ' \n')" != 02 ]; then
			fail "$(L "The archive's SLSsteam.so is not a 64-bit build; the Steam client here is 64-bit." \
			          "O SLSsteam.so do pacote não é 64 bits; o cliente Steam aqui é 64 bits.")"
		fi

		chmod +x "$setup" 2>/dev/null || true
		log_info "$(L "Running slsteam-moon setup (this will stop Steam)" \
		             "Rodando o setup do slsteam-moon (isto vai parar a Steam)")"
		setup_path="$(path_without_slsteam_wrapper "${PATH:-}")"
		( cd "$extract_root" && PATH="$setup_path" bash "$setup" install ) \
			|| fail "$(L "slsteam-moon setup failed" "Falha no setup do slsteam-moon")"

		log_success "$(L "slsteam-moon (x86_64) installed" "slsteam-moon (x86_64) instalado")"
		seed_slsteam_config "$extract_root/res/config.yaml"
	}

	install_cloudredirect() {
		slsm_client_is_64bit || { orig_install_cloudredirect; return; }
		log_warn "$(L "CloudRedirect is not available with the 64-bit Steam client (its hook is 32-bit); skipping cloud saves." \
		             "O CloudRedirect não está disponível com o cliente Steam de 64 bits (o hook é 32 bits); pulando cloud saves.")"
		# A leftover 32-bit hook would make the wrapper LD_PRELOAD a library the client ignores.
		set_disable_cloud yes
		return 0
	}
}

main_x86_64() {
	load_stock_installer
	define_overrides
	if slsm_client_is_64bit; then
		log_info "$(L "64-bit Steam client detected: using the x86_64 slsteam-moon build." \
		             "Cliente Steam de 64 bits detectado: usando o build x86_64 do slsteam-moon.")"
	else
		local reason; reason="$(slsm_32bit_reason)"
		if [ "${SLSM_ALLOW_STOCK:-0}" != 1 ]; then
			log_warn "$(L "Stopping: this installer is for the 64-bit Steam client, and ${reason}." \
			             "Parando: este instalador é para o cliente Steam de 64 bits, e ${reason}.")"
			log_info "$(L "Nothing was changed. Remove that setting (or start Steam once so it downloads the 64-bit client), then run this installer again. For the 32-bit client use https://github.com/swwayps/luatools-moon instead. To install the upstream 32-bit build anyway: SLSM_ALLOW_STOCK=1." \
			             "Nada foi alterado. Remova essa configuração (ou abra a Steam uma vez para baixar o cliente de 64 bits) e rode este instalador de novo. Para o cliente de 32 bits use https://github.com/swwayps/luatools-moon. Para instalar mesmo assim o build de 32 bits do upstream: SLSM_ALLOW_STOCK=1.")"
			exit 1
		fi
		log_warn "$(L "SLSM_ALLOW_STOCK=1: installing the upstream 32-bit build because ${reason}." \
		             "SLSM_ALLOW_STOCK=1: instalando o build de 32 bits do upstream porque ${reason}.")"
	fi
	main "${PASSTHRU[@]+"${PASSTHRU[@]}"}"
	[ "${SLSM_REPOINT_FAILED:-0}" = 0 ] || exit 1
}

# Sourced by tests with SLSM_X64_LIB_ONLY=1.
if [ -z "${SLSM_X64_LIB_ONLY:-}" ]; then
	main_x86_64
fi
