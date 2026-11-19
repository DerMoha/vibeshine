#!/usr/bin/env bash
# Shared os-release classification. Optional path supports isolated fixtures.
detect_linux_distribution() {
  local release_file=${1:-/etc/os-release} ID='' ID_LIKE='' VERSION_ID=''
  [[ -r "$release_file" ]] || return 1
  # shellcheck source=/dev/null
  source "$release_file"
  os_id=$ID
  os_version=$VERSION_ID
  case "$ID" in
    fedora|nobara) os_family=fedora ;;
    arch|cachyos) os_family=arch ;;
    *)
      case " $ID_LIKE " in
        *' fedora '*) os_family=fedora ;;
        *' arch '*) os_family=arch ;;
        *) os_family=$ID ;;
      esac ;;
  esac
}
