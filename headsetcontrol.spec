Name:           headsetcontrol
Version:        4.0.0
Release:        1%{?dist}
Summary:        Control features of USB gaming headsets

License:        GPL-3.0-only
URL:            https://github.com/Sapd/HeadsetControl
Source0:        %{url}/archive/refs/tags/%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  hidapi-devel
BuildRequires:  pkgconfig
BuildRequires:  systemd-rpm-macros

%description
HeadsetControl is a tool to control USB-connected gaming headsets: sidetone,
battery status, LEDs, inactive-time auto-shutoff, chatmix, equalizer presets
and more, for a wide range of Logitech, SteelSeries, Corsair, HyperX, Roccat
and Audeze devices. On Linux it installs udev rules so the headsets are
accessible without root privileges.

%prep
%autosetup -n HeadsetControl-%{version}
# Release tarballs have no git metadata; inject the version so the binary does
# not report 0.0.0-unknown. (From the next release, -DHEADSETCONTROL_VERSION
# handles this and this sed can be dropped.)
sed -i 's/git describe --tags --dirty=-modified/echo %{version}/' CMakeLists.txt

%build
%cmake -DHEADSETCONTROL_VERSION=%{version}
%cmake_build

%install
%cmake_install
# CLI package: drop the static library and dev headers
rm -f %{buildroot}%{_prefix}/lib/libheadsetcontrol*
rm -rf %{buildroot}%{_includedir}/headsetcontrol

%files
%license license
%doc README.md
%{_bindir}/headsetcontrol
%{_udevrulesdir}/70-headsets.rules

%changelog
* Thu Jul 23 2026 Denis Arnst <git@sapd.eu> - 4.0.0-1
- Initial COPR packaging
