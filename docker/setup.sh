#!/usr/bin/env bash

set -eux

install_debian()
{
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -yq \
       python2.7 \
       g++ \
       libxml2-dev \
       libnetfilter-queue-dev

    ln -s /usr/bin/python2.7 /usr/bin/python

    if [ "$VERSION" != "" ]; then
        apt-get install -yq git-core
        cd $NORM_DIR && git checkout $VERSION && git submodule update --checkout --init --force
    fi

    if [[ "$WAF_CONFIGURE" =~ "java" && -z `which java` ]]; then
        apt-get install -yq default-jdk-headless
        export JAVA_HOME=$(readlink -f `which java` | sed 's/\/bin\/java//')
    fi
}

install_redhat()
{
    yum update -y
    yum install -y \
      which \
      centos-release-scl
    yum install -y \
      gcc-c++ \
      libxml2-devel \
      libnetfilter_queue-devel

    if [ "$VERSION" != "" ]; then
        yum install -y git
        cd $NORM_DIR && git checkout $VERSION && git submodule update --checkout --init --force
    fi

    if [[ "$WAF_CONFIGURE" =~ "java" && -z `which java` ]]; then
        yum install -y java-1.8.0-openjdk-devel
        export JAVA_HOME=$(readlink -f `which java` | sed 's/\/jre\/bin\/java//')
    fi
}

configure_norm()
{
    cd $NORM_DIR && ./waf configure ${WAF_CONFIGURE}
}

if [ ! -z `which apt` ]; then
    install_debian
elif [ ! -z $(which yum) ] || [ "$?" = "1" ]; then
    install_redhat
fi
configure_norm
