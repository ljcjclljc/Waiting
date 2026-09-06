#!/usr/bin/env bash
set -euo pipefail

readonly PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
readonly STATE_DIRECTORY=/var/lib/chen-blog-content
readonly ASSET_ROOT=/opt/chen-blog
readonly KNOWLEDGE_DIRECTORY="${ASSET_ROOT}/knowledge_base"
readonly PROMPT_DIRECTORY="${ASSET_ROOT}/prompt_optimization"
readonly VERSION_FILE="${ASSET_ROOT}/.ai-assets-version"
readonly MAX_BYTES=$((16 * 1024 * 1024))
readonly MAX_FILES=1000

die() { printf 'AI asset deployment failed: %s\n' "$*" >&2; exit 1; }
[[ $# -eq 1 ]] || die 'expected one asset digest'
readonly DIGEST="$1"
[[ "${DIGEST}" =~ ^[0-9a-f]{40}$ ]] || die 'invalid asset digest'

for command_name in awk basename chmod cut du find flock grep head install mktemp mv rm rsync sha256sum tar tr wc; do
    command -v "${command_name}" >/dev/null 2>&1 || die "required command is missing: ${command_name}"
done

umask 022
install -d -m 0755 "${STATE_DIRECTORY}" "${KNOWLEDGE_DIRECTORY}" "${PROMPT_DIRECTORY}"
exec 9>"${STATE_DIRECTORY}/ai-assets-deploy.lock"
flock -w 180 9 || die 'another AI asset deployment is still running'

readonly INCOMING_DIRECTORY="${STATE_DIRECTORY}/incoming"
readonly INCOMING_FILE="${INCOMING_DIRECTORY}/${DIGEST}.tar"
readonly INCOMING_TMP="${INCOMING_FILE}.tmp"
install -d -m 0700 "${INCOMING_DIRECTORY}"
rm -f -- "${INCOMING_TMP}"
head -c $((MAX_BYTES + 1)) >"${INCOMING_TMP}"
readonly INCOMING_SIZE="$(wc -c <"${INCOMING_TMP}")"
[[ "${INCOMING_SIZE}" -gt 0 && "${INCOMING_SIZE}" -le "${MAX_BYTES}" ]] || die 'invalid asset bundle size'
readonly ACTUAL_DIGEST="$(sha256sum "${INCOMING_TMP}" | cut -c1-40)"
[[ "${ACTUAL_DIGEST}" == "${DIGEST}" ]] || die "asset digest mismatch: expected ${DIGEST}, got ${ACTUAL_DIGEST}"
mv -f -- "${INCOMING_TMP}" "${INCOMING_FILE}"

readonly STAGE_DIRECTORY="$(mktemp -d "${STATE_DIRECTORY}/ai-assets-stage.XXXXXX")"
readonly BACKUP_DIRECTORY="$(mktemp -d "${STATE_DIRECTORY}/ai-assets-backup.XXXXXX")"
readonly STAGE_KNOWLEDGE="${STAGE_DIRECTORY}/knowledge_base"
readonly STAGE_PROMPT="${STAGE_DIRECTORY}/prompt_optimization"
deployment_applied=false
previous_version=''
had_previous_version=false

cleanup() { rm -rf -- "${STAGE_DIRECTORY}" "${BACKUP_DIRECTORY}" "${INCOMING_FILE}" "${INCOMING_TMP}"; }
rollback() {
    if [[ "${deployment_applied}" != true ]]; then return; fi
    printf 'AI asset verification failed; restoring previous files.\n' >&2
    rsync -a --delete --delay-updates --chmod=D755,F644 \
        "${BACKUP_DIRECTORY}/knowledge_base/" "${KNOWLEDGE_DIRECTORY}/"
    rsync -a --delete --delay-updates --chmod=D755,F644 \
        "${BACKUP_DIRECTORY}/prompt_optimization/" "${PROMPT_DIRECTORY}/"
    if [[ "${had_previous_version}" == true ]]; then
        printf '%s\n' "${previous_version}" >"${VERSION_FILE}.rollback"
        mv -f -- "${VERSION_FILE}.rollback" "${VERSION_FILE}"
    else
        rm -f -- "${VERSION_FILE}"
    fi
}
on_exit() {
    local status=$?
    if [[ ${status} -ne 0 ]]; then rollback || true; fi
    cleanup
    exit "${status}"
}
trap on_exit EXIT

if tar -tf "${INCOMING_FILE}" | grep -E '(^/|^[A-Za-z]:/|(^|/)\.\.(\/|$))' >/dev/null; then
    die 'asset bundle contains unsafe paths'
fi
while IFS= read -r tar_entry; do
    case "${tar_entry}" in
        knowledge_base|knowledge_base/*|prompt_optimization|prompt_optimization/*) ;;
        *) die "asset bundle contains an unexpected path: ${tar_entry}" ;;
    esac
done < <(tar -tf "${INCOMING_FILE}")
if tar -tvf "${INCOMING_FILE}" | grep -Eq '^l'; then
    die 'asset bundle contains symlinks'
fi
tar -xf "${INCOMING_FILE}" -C "${STAGE_DIRECTORY}" --no-same-owner --no-same-permissions --no-overwrite-dir
[[ -d "${STAGE_KNOWLEDGE}" && -d "${STAGE_PROMPT}" ]] || die 'bundle must contain both asset directories'

for directory in "${STAGE_KNOWLEDGE}" "${STAGE_PROMPT}"; do
    if find "${directory}" -type l -print -quit | grep -q .; then die 'asset symlinks are not allowed'; fi
    if find "${directory}" ! -type f ! -type d -print -quit | grep -q .; then die 'assets may contain only regular files and directories'; fi
done
while IFS= read -r -d '' asset_file; do
    asset_extension="${asset_file##*.}"
    asset_extension="${asset_extension,,}"
    case "${asset_extension}" in
        md|markdown|txt|json|csv|yaml|yml) ;;
        *) die "unsupported asset file type: ${asset_file}" ;;
    esac
done < <(find "${STAGE_KNOWLEDGE}" "${STAGE_PROMPT}" -type f -print0)
file_count="$(find "${STAGE_KNOWLEDGE}" "${STAGE_PROMPT}" -type f | wc -l)"
byte_count="$(du -sb "${STAGE_KNOWLEDGE}" "${STAGE_PROMPT}" | awk '{ total += $1 } END { print total + 0 }')"
[[ "${file_count}" -gt 0 ]] || die 'asset bundle contains no files'
[[ "${file_count}" -le "${MAX_FILES}" ]] || die 'too many asset files'
[[ "${byte_count}" -le "${MAX_BYTES}" ]] || die 'asset bundle is too large'

install -d -m 0755 "${BACKUP_DIRECTORY}/knowledge_base" "${BACKUP_DIRECTORY}/prompt_optimization"
rsync -a "${KNOWLEDGE_DIRECTORY}/" "${BACKUP_DIRECTORY}/knowledge_base/"
rsync -a "${PROMPT_DIRECTORY}/" "${BACKUP_DIRECTORY}/prompt_optimization/"
if [[ -f "${VERSION_FILE}" ]]; then
    previous_version="$(tr -d '\r\n' <"${VERSION_FILE}")"
    had_previous_version=true
fi

deployment_applied=true
rsync -a --delete --delay-updates --chmod=D755,F644 \
    "${STAGE_KNOWLEDGE}/" "${KNOWLEDGE_DIRECTORY}/"
rsync -a --delete --delay-updates --chmod=D755,F644 \
    "${STAGE_PROMPT}/" "${PROMPT_DIRECTORY}/"
printf '%s\n' "${DIGEST}" >"${VERSION_FILE}.tmp"
chmod 0644 "${VERSION_FILE}.tmp"
mv -f -- "${VERSION_FILE}.tmp" "${VERSION_FILE}"

printf 'AI asset deployment succeeded at %s (%s files).\n' "${DIGEST}" "${file_count}"
