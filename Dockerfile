ARG platformName=ubuntu
ARG platformVersion=latest
FROM ${platformName}:${platformVersion}

ARG version
ARG configure
ARG build="--targets=*"

ENV NORM_DIR=/norm-staging
ENV NORM_BUILD=$NORM_DIR/build
ENV WAF_CONFIGURE=${configure}
ENV WAF_BUILD=${build}

ADD ./ ${NORM_DIR}

# Step 1: dependencies
RUN VERSION=${version} ${NORM_DIR}/docker/setup.sh

# Step 2: build
RUN cd ${NORM_DIR} && ./waf ${WAF_BUILD}

# Step 3: install
RUN cd ${NORM_DIR} && ./waf install

# Step 4: expose
COPY ./docker/run.sh /usr/bin/run-norm-example
ENTRYPOINT [ "run-norm-example" ]
