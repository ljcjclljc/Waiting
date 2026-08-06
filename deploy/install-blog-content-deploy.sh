#!/usr/bin/env bash
set -euo pipefail

readonly PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

die() {
    printf 'Installer failed: %s\n' "$*" >&2
    exit 1
}

[[ "$(id -u)" -eq 0 ]] || die "run this installer as root"
[[ $# -eq 1 ]] || die "expected the publish public-key file"
readonly PUBLIC_KEY_FILE="$1"
[[ -f "$PUBLIC_KEY_FILE" ]] || die "public-key file not found"

readonly SCRIPT_DIRECTORY="$(cd "$(dirname "$BASH_SOURCE")" && pwd)"
readonly DEPLOY_SCRIPT="$SCRIPT_DIRECTORY/deploy-blog-content.sh"
readonly COMMAND_SCRIPT="$SCRIPT_DIRECTORY/blog-content-deploy-command.sh"
readonly AUTHORIZED_KEYS="/home/blog-deploy/.ssh/authorized_keys"

bash -n "$DEPLOY_SCRIPT" "$COMMAND_SCRIPT"
for command_name in curl docker flock rsync sha256sum sudo tar visudo; do
    command -v "$command_name" >/dev/null 2>&1 ||
        die "required command is missing: $command_name"
done

if id blog-deploy >/dev/null 2>&1; then
    usermod --shell /bin/bash blog-deploy
else
    useradd --create-home --shell /bin/bash blog-deploy
fi
passwd --lock blog-deploy >/dev/null

install -o root -g root -m 0755 "$DEPLOY_SCRIPT" /usr/local/sbin/deploy-blog-content
install -o root -g root -m 0755 "$COMMAND_SCRIPT" /usr/local/sbin/blog-content-deploy-command
install -d -o root -g root -m 0755 /var/lib/chen-blog-content
install -d -o blog-deploy -g blog-deploy -m 0700 /home/blog-deploy/.ssh
touch "$AUTHORIZED_KEYS"
chmod 0600 "$AUTHORIZED_KEYS"
chown blog-deploy:blog-deploy "$AUTHORIZED_KEYS"

read -r key_type key_blob key_comment <"$PUBLIC_KEY_FILE"
[[ "$key_type" == "ssh-ed25519" && -n "$key_blob" ]] ||
    die "expected an Ed25519 public key"
if [[ -z "$key_comment" ]]; then
    key_comment="local-blog-publish"
fi
sed -i '/ github-actions-blog-content\(-v[0-9][0-9]*\)\?$/d' "$AUTHORIZED_KEYS"
sed -i '/ local-blog-publish\(-v[0-9][0-9]*\)\?$/d' "$AUTHORIZED_KEYS"
sed -i "\|$key_blob|d" "$AUTHORIZED_KEYS"
printf 'command="/usr/local/sbin/blog-content-deploy-command",restrict %s %s %s\n' "$key_type" "$key_blob" "$key_comment" >>"$AUTHORIZED_KEYS"

printf '%s\n' 'blog-deploy ALL=(root) NOPASSWD: /usr/local/sbin/deploy-blog-content *' >/etc/sudoers.d/blog-content-deploy
chmod 0440 /etc/sudoers.d/blog-content-deploy
visudo -cf /etc/sudoers.d/blog-content-deploy >/dev/null

if command -v restorecon >/dev/null 2>&1; then
    restorecon -R /home/blog-deploy/.ssh >/dev/null 2>&1 || true
fi

printf 'Restricted blog content publish account installed.\n'
