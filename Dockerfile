FROM --platform=linux/amd64 ubuntu:18.04
ENV LANG C.UTF-8
ENV LC_ALL C.UTF-8
RUN apt-get -y update
RUN apt-get install -y curl python3 build-essential python3-venv git libpython-dev python3-dev libssl-dev
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs > rustup.sh
RUN sh ./rustup.sh -y
RUN python3 -c "$(curl -fsSL https://raw.githubusercontent.com/platformio/platformio/master/scripts/get-platformio.py)"
ENV PATH="${PATH}:/root/.platformio/penv/bin:/root/.cargo/bin"
RUN pip3 install --upgrade pip
RUN pip3 install setuptools_rust pyparsing cryptography
COPY platformio.ini /root
WORKDIR /root
RUN platformio run --target clean
WORKDIR /data
CMD /bin/bash
