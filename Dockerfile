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

RUN apt-get update && apt-get install -y --no-install-recommends \
    libopenmpi-dev openmpi-bin && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt/ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}/build/scratch/ghostdagsim/ns${NS3_VERSION}-ghostdagsim-optimized /usr/local/bin/ghostdagsim
COPY --from=builder /opt/ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}/build/lib/ /usr/local/lib/ns3/

ENV LD_LIBRARY_PATH=/usr/local/lib/ns3
ENV MPI_THREADS=1

WORKDIR /results

ENTRYPOINT ["/bin/sh", "-c", "exec mpirun --allow-run-as-root -np ${MPI_THREADS:-1} /usr/local/bin/ghostdagsim \"$@\""]
