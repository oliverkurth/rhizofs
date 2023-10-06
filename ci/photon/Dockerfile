FROM photon:5.0

RUN tdnf makecache && \
    tdnf -y update && \
    tdnf install -y \
        rpm-build \
        build-essential \
        git

