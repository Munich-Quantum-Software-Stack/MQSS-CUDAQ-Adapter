FROM --platform=x86_64 python:3.11-bookworm

RUN apt-get update -y && \
    apt-get autoremove -y

RUN apt-get install -y software-properties-common bash-completion cmake build-essential iputils-ping rsync p7zip-full libuv1-dev git librabbitmq-dev libboost-dev libgtest-dev pybind11-dev libgmock-dev flex bison

RUN git clone --branch v3.12.0 https://github.com/nlohmann/json.git && cd json/ && mkdir build && cd build && cmake .. -DJSON_BuildTests=OFF && make install && cd ../.. && rm -rf json

ENV CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt

WORKDIR /home

CMD ["bash"]
