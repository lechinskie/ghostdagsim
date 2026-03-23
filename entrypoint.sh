#!/bin/sh
# =============================================================================
# ghostdagsim entrypoint
#
# Single-machine:
#   docker run -e MPI_THREADS=4 ghostdagsim --nodes=100
#
#
# Full manual control — MPI_ARGS overrides everything:
#   docker run \
#     -e MPI_ARGS="--hostfile /hostfile --mca btl_tcp_if_include eth0 -np 12" \
#     -v ./my-hostfile:/hostfile \
#     ghostdagsim --nodes=100
# =============================================================================

set -e
if [ "$1" = "--" ]; then
  shift
fi

if command -v sshd >/dev/null 2>&1; then
  if [ ! -f /etc/ssh/ssh_host_rsa_key ]; then
    ssh-keygen -A >/dev/null 2>&1
  fi
  /usr/sbin/sshd
fi

if [ -n "${MPI_ARGS}" ]; then
  echo "[ghostdagsim] Using custom MPI_ARGS: ${MPI_ARGS}"
	echo "$@"
  exec mpirun --allow-run-as-root ${MPI_ARGS} /usr/local/bin/ghostdagsim "$@"
else
  NP="${MPI_THREADS:-1}"
  echo "[ghostdagsim] Single-machine mode: ${NP} MPI rank(s)"
	echo "$@"
  exec mpirun --allow-run-as-root \
    -np "${NP}" \
    /usr/local/bin/ghostdagsim "$@"
fi
