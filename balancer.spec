Name:           balancer
Version:        0
Release:        1%{?dist}
Summary:        reverse proxy / load balancer and a monitoring service

License:        MIT
URL:            https://github.com/longlodwyorku/balancer
Source0:        %URL/archive/refs/tags/v%{version}.tar.gz

BuildRequires:  g++ 
BuildRequires:  libmonitor-devel
BuildRequires:  make

%description
Balancer is a package containing a reverse proxy / load balancer and a monitoring service. The reverse proxy / load balancer is a simple server that listens for incoming requests and forwards them to a backend server. The monitoring service is a simple server that broadcast the CPU and memory usage of backend server to the load balancer. The broadcasted information will be used for load balancing.

%package proxy
Summary:        reverse proxy / load balancer server
Requires:       balancer = %{version}-%{release}

%description proxy
The %name-proxy package contains the reverse proxy / load balancer server.

%package monitor
Summary:        monitoring service
Requires:       balancer = %{version}-%{release}
Requires:       libmonitor

%description monitor
The %name-monitor package contains the monitoring service.

%global debug_package %{nil}

%prep
%autosetup


%build
%make_build


%install
rm -rf $RPM_BUILD_ROOT
%make_install

%{?ldconfig_scriptlets}


%files
%license LICENSE

%files proxy
%{_bindir}/balancer-proxy.sh
%{_bindir}/balancer-proxy
%{_sysconfdir}/balancer/proxy.conf
%{_exec_prefix}/lib/systemd/system/balancer-proxy.service

%files monitor
%{_bindir}/balancer-monitor.sh
%{_bindir}/balancer-monitor
%{_sysconfdir}/balancer/monitor.conf
%{_exec_prefix}/lib/systemd/system/balancer-monitor.service


%changelog
* Sun Jun 09 2024 longlodw@my.yorku.ca
- 
