Name:       rhizofs
Version:    0.2.2
Release:    1%{?dist}
URL:        https://github.com/oliverkurth/rhizofs
Source0:    %{name}-%{version}.tar.gz
Summary:    A simple remote filesystem based on FUSE, ZeroMQ and protobuf-c
License:    BSD

BuildRequires: protobuf-c-devel
BuildRequires: fuse-devel
BuildRequires: zeromq-devel

%description
This package contains the client.

%package        server
Summary:        RhizoFS server

%description    server
This package contains the server.

%prep
%autosetup

%build
%make_build

%install
mkdir -p %{buildroot}/%{_bindir}
export PREFIX=%{buildroot}/%{_prefix}
%make_install

%files
%{_bindir}/rhizofs

%files server
%{_bindir}/rhizosrv

%changelog
* Mon May 23 2022 <okurth@vmware.com> 0.2.2-1
- initial rpm package
