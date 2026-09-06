Name:       rhizofs
# a "^" in Version marks a post-release snapshot: RPM's version comparison
# treats it as newer than the plain release it follows (0.2.7^git30.b8a4ec6
# > 0.2.7), per Fedora's snapshot-versioning convention. It has to live in
# Version rather than Release, since %%{?dist} trails Release and the caret
# only outranks a competing string that ends right at that point -- with
# %%{?dist} appended after it on both sides, it would sort the wrong way.
# Assumes rpmbuild is invoked from the repository root (as
# ci/photon/build-rpms.sh does).
%define _snapshot %(s=$(scripts/get-version.sh 2>/dev/null); [ -n "$s" ] && printf '^%s' "$s")
Version:    0.2.7%{_snapshot}
Release:    1%{?dist}
URL:        https://github.com/oliverkurth/rhizofs
Source0:    %{name}-%{version}.tar.gz
Summary:    A simple remote filesystem based on FUSE, ZeroMQ and protobuf-c
License:    BSD

BuildRequires: protobuf-c-devel
BuildRequires: fuse3-devel
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
export USERUNITDIR=%{buildroot}%{_userunitdir}
%make_install

%files
%{_bindir}/rhizofs
%{_bindir}/rhizofs-setuptool.sh
%{_bindir}/rhizo-keygen
%{_userunitdir}/rhizofs@.service

%files server
%{_bindir}/rhizosrv
%{_bindir}/rhizosrv-setuptool.sh
%{_bindir}/rhizo-keygen
%{_userunitdir}/rhizosrv@.service

%changelog
* Sat Apr 27 2024 <okurth@gmail.com> 0.2.7-1
- update to 0.2.7
* Sat Apr 20 2024 <okurth@gmail.com> 0.2.6-1
- update to 0.2.6
* Mon Jan 15 2024 <okurth@gmail.com> 0.2.5-1
- update to 0.2.5
* Fri Oct 06 2023 <okurth@gmail.com> 0.2.4-1
- update to 0.2.4
* Mon May 23 2022 <okurth@gmail.com> 0.2.2-1
- initial rpm package
