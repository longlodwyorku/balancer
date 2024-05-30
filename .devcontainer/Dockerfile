FROM rockylinux:9
RUN dnf -y update && dnf clean all
RUN dnf install -y epel-release
RUN dnf install -y nodejs npm unzip git gcc rpm-build rpm-devel rpmlint make diffutils patch rpmdevtools gcc-c++ neovim iproute
RUN git clone https://github.com/longlodw/nvim.git /root/.config/nvim && cd /root/.config/nvim && git checkout cpp
RUN curl -L https://github.com/longlodwyorku/libmonitor/releases/download/v0/libmonitor-0-1.el9.x86_64.rpm -o libmonitor-0-1.rpm
RUN dnf localinstall -y libmonitor-0-1.rpm && rm libmonitor-0-1.rpm
RUN curl -L https://github.com/longlodwyorku/libmonitor/releases/download/v0/libmonitor-devel-0-1.el9.x86_64.rpm -o libmonitor-devel-0-1.rpm
RUN dnf localinstall -y libmonitor-devel-0-1.rpm && rm libmonitor-devel-0-1.rpm
ARG USER
ARG UID
RUN useradd -m $USER -u $UID
RUN mkdir /home/$USER/.config && cp -r /root/.config/nvim /home/$USER/.config/nvim && chown -R $USER:$USER /home/$USER/.config
USER $USER
RUN rpmdev-setuptree
