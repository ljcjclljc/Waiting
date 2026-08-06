#!/usr/bin/env bash
set -euo pipefail

readonly PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
readonly REPOSITORY_URL="https://github.com/ljcjclljc/Waiting.git"
readonly STATE_DIRECTORY="/var/lib/chen-blog-content"
readonly REPOSITORY_DIRECTORY="${STATE_DIRECTORY}/repo.git"
readonly CONTENT_DIRECTORY="/opt/chen-blog/content"
readonly POSTS_DIRECTORY="${CONTENT_DIRECTORY}/posts"
readonly VERSION_FILE="${CONTENT_DIRECTORY}/.content-version"
readonly CONTAINER_NAME="chen-blog-blog-1"
readonly HEALTH_URL="http://127.0.0.1:8080/health"
readonly MAX_FILES=1100
readonly MAX_BYTES=$((64 * 1024 * 1024))

die() {
    printf 'Content deployment failed: %s\n' "$*" >&2
    exit 1
}

[[ $# -eq 1 ]] || die "expected one commit SHA"
readonly COMMIT_SHA="$1"
[[ "${COMMIT_SHA}" =~ ^[0-9a-f]{40}$ ]] || die "invalid commit SHA"

for command_name in curl docker flock git rsync tar; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

umask 022
install -d -m 0755 "${STATE_DIRECTORY}" "${CONTENT_DIRECTORY}"
exec 9>"${STATE_DIRECTORY}/deploy.lock"
flock -w 180 9 || die "another content deployment is still running"

if [[ ! -d "${REPOSITORY_DIRECTORY}" ]]; then
    git init --bare "${REPOSITORY_DIRECTORY}" >/dev/null
fi
if git --git-dir="${REPOSITORY_DIRECTORY}" remote get-url origin >/dev/null 2>&1; then
    git --git-dir="${REPOSITORY_DIRECTORY}" remote set-url origin "${REPOSITORY_URL}"
else
    git --git-dir="${REPOSITORY_DIRECTORY}" remote add origin "${REPOSITORY_URL}"
fi

fetched=false
for attempt in 1 2 3; do
    if git --git-dir="${REPOSITORY_DIRECTORY}" fetch \
        --force --no-tags --depth=1 origin \
        "+refs/heads/main:refs/heads/deploy-main"; then
        fetched=true
        break
    fi
    printf 'Fetch attempt %d failed; retrying...\n' "${attempt}" >&2
    sleep $((attempt * 3))
done
[[ "${fetched}" == true ]] || die "could not fetch ${COMMIT_SHA}"
main_sha="$(git --git-dir="${REPOSITORY_DIRECTORY}" \
    rev-parse refs/heads/deploy-main)"
[[ "${COMMIT_SHA}" == "${main_sha}" ]] ||
    die "${COMMIT_SHA} is not the current main commit (${main_sha})"

readonly STAGE_DIRECTORY="$(mktemp -d "${CONTENT_DIRECTORY}/.deploy-stage.XXXXXX")"
readonly BACKUP_DIRECTORY="$(mktemp -d "${STATE_DIRECTORY}/backup.XXXXXX")"
readonly STAGE_NAME="$(basename "${STAGE_DIRECTORY}")"
readonly STAGED_POSTS="${STAGE_DIRECTORY}/content/posts"
chmod 0755 "${STAGE_DIRECTORY}"
deployment_applied=false
previous_version=""
had_previous_version=false

cleanup() {
    rm -rf -- "${STAGE_DIRECTORY}" "${BACKUP_DIRECTORY}"
}

rollback() {
    if [[ "${deployment_applied}" != true ]]; then
        return
    fi
    printf 'Health verification failed; restoring previous content.\n' >&2
    if [[ -d "${BACKUP_DIRECTORY}/posts" ]]; then
        install -d -m 0755 "${POSTS_DIRECTORY}"
        rsync -a --delete --delay-updates --chmod=D755,F644 \
            "${BACKUP_DIRECTORY}/posts/" "${POSTS_DIRECTORY}/"
    else
        rm -rf -- "${POSTS_DIRECTORY}"
    fi
    if [[ "${had_previous_version}" == true ]]; then
        printf '%s\n' "${previous_version}" >"${VERSION_FILE}.rollback"
        mv -f -- "${VERSION_FILE}.rollback" "${VERSION_FILE}"
    else
        rm -f -- "${VERSION_FILE}"
    fi
}

on_exit() {
    status=$?
    if [[ ${status} -ne 0 ]]; then
        rollback || true
    fi
    cleanup
    exit "${status}"
}
trap on_exit EXIT

git --git-dir="${REPOSITORY_DIRECTORY}" archive "${COMMIT_SHA}" content/posts |
    tar -x -C "${STAGE_DIRECTORY}"
[[ -d "${STAGED_POSTS}" ]] || die "commit does not contain content/posts"

if find "${STAGED_POSTS}" -type l -print -quit | grep -q .; then
    die "content symlinks are not allowed"
fi
if find "${STAGED_POSTS}" ! -type f ! -type d -print -quit | grep -q .; then
    die "content may contain only regular files and directories"
fi
file_count="$(find "${STAGED_POSTS}" -type f | wc -l)"
[[ "${file_count}" -le "${MAX_FILES}" ]] || die "too many content files"
byte_count="$(du -sb "${STAGED_POSTS}" | awk '{print $1}')"
[[ "${byte_count}" -le "${MAX_BYTES}" ]] || die "content is too large"

docker exec "${CONTAINER_NAME}" /app/drogon_blog --validate-content \
    "/app/content/${STAGE_NAME}/content/posts"

if [[ -f "${VERSION_FILE}" ]]; then
    previous_version="$(tr -d '\r\n' <"${VERSION_FILE}")"
    had_previous_version=true
fi
if [[ -d "${POSTS_DIRECTORY}" ]]; then
    install -d -m 0755 "${BACKUP_DIRECTORY}/posts"
    rsync -a "${POSTS_DIRECTORY}/" "${BACKUP_DIRECTORY}/posts/"
fi

install -d -m 0755 "${POSTS_DIRECTORY}"
rsync -a --delete --delay-updates --chmod=D755,F644 \
    "${STAGED_POSTS}/" "${POSTS_DIRECTORY}/"
deployment_applied=true
printf '%s\n' "${COMMIT_SHA}" >"${VERSION_FILE}.tmp"
chmod 0644 "${VERSION_FILE}.tmp"
mv -f -- "${VERSION_FILE}.tmp" "${VERSION_FILE}"

for attempt in $(seq 1 30); do
    health="$(curl --fail --silent --show-error --max-time 3 "${HEALTH_URL}" || true)"
    if grep -Eq '"contentReload"[[:space:]]*:[[:space:]]*"up"' <<<"${health}" &&
        grep -Eq "\"contentVersion\"[[:space:]]*:[[:space:]]*\"${COMMIT_SHA}\"" <<<"${health}"; then
        deployment_applied=false
        printf 'Content deployment succeeded at %s.\n' "${COMMIT_SHA}"
        exit 0
    fi
    sleep 2
done

die "Drogon did not activate ${COMMIT_SHA} within 60 seconds"
