ARG NS3_VERSION=3.46.1

FROM debian:bookworm-slim AS builder

ARG NS3_VERSION

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake g++ python3 \
    libopenmpi-dev openmpi-bin \
    wget git bzip2 ca-certificates tar && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /opt
RUN wget -q https://www.nsnam.org/release/ns-allinone-${NS3_VERSION}.tar.bz2 && \
    tar xjf ns-allinone-${NS3_VERSION}.tar.bz2 && \
    rm ns-allinone-${NS3_VERSION}.tar.bz2

WORKDIR /opt/ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}

RUN ./ns3 configure --build-profile=optimized --enable-mpi -- -DGHOSTDAGSIM_METRICS=ON && \
    ./ns3 build

COPY . /opt/ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}/scratch/ghostdagsim/
RUN ./ns3 build ghostdagsim

FROM debian:bookworm-slim

ARG NS3_VERSION

LABEL org.opencontainers.image.title="ghostdagsim" \
      org.opencontainers.image.description="GHOSTDAG consensus protocol network simulator (ns-3 + MPI)" \
      org.opencontainers.image.source="https://github.com/lechinskie/ghostdagsim" \
      org.opencontainers.image.licenses="GPL-2.0"

RUN apt-get update && apt-get install -y --no-install-recommends \
    libopenmpi-dev openmpi-bin \
    openssh-server openssh-client && \
    rm -rf /var/lib/apt/lists/*

# This is only used when multiple containers need to communicate via MPI.
# The authorized_keys file is intentionally left empty here; mount or inject
# your cluster's public key at runtime via:
#   docker run -v ~/.ssh/id_rsa.pub:/root/.ssh/authorized_keys:ro ...
RUN mkdir -p /root/.ssh /var/run/sshd && \
    chmod 700 /root/.ssh && \
    echo "PermitRootLogin yes" >> /etc/ssh/sshd_config && \
    echo "StrictHostKeyChecking no" > /root/.ssh/config && \
    echo "UserKnownHostsFile /dev/null" >> /root/.ssh/config

COPY --from=builder \
    /opt/ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}/build/scratch/ghostdagsim/ns${NS3_VERSION}-ghostdagsim-optimized \
    /usr/local/bin/ghostdagsim

COPY --from=builder \
    /opt/ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}/build/lib/ \
    /usr/local/lib/ns3/

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENV LD_LIBRARY_PATH=/usr/local/lib/ns3

# MPI_THREADS : number of local MPI ranks
# MPI_ARGS    : raw mpirun arguments — overrides threads
ENV MPI_THREADS=1

WORKDIR /results

EXPOSE 22

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
