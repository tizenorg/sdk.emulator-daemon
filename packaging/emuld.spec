Name: emuld
Version: 0.10.3
Release: 0
Summary: Emulator daemon
License: Apache-2.0
Source0: %{name}-%{version}.tar.gz
Group: SDK/Other
ExclusiveArch: %{ix86}

BuildRequires: cmake
BuildRequires: pkgconfig(vconf)
BuildRequires: pkgconfig(deviced)
BuildRequires: pkgconfig(dlog)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(edbus)
BuildRequires: pkgconfig(ecore)
BuildRequires: pkgconfig(capi-network-connection)

%description
A emulator daemon is used for communication between guest and host

%package emuld
Summary:    Emulator daemon
Requires:   libemuld = %{version}-%{release}

%description emuld
Emulator daemon

%package -n libemuld
Summary:    Emulator daemon library
Requires:   vconf

%description -n libemuld
Emulator daemon library

%package -n libemuld-devel
Summary:    Emulator daemon library for (devel)

%description -n libemuld-devel
Emulator daemon library for emuld plugins

%prep
%setup -q

# Default msgproc configuration
%define msgproc_hds on
%define msgproc_cmd on
%define msgproc_package on
%define msgproc_system on
%define msgproc_vconf on
%define msgproc_suspend on

%if "%{?tizen_profile_name}" == "mobile"
%define msgproc_location on
%endif
%if "%{?tizen_profile_name}" == "wearable"
%define msgproc_location on
%endif

%if "%{?tizen_profile_name}" == "mobile"
export CFLAGS+=" -DMOBILE"
%else
%if "%{?tizen_profile_name}" == "wearable"
export CFLAGS+=" -DWEARABLE"
%else
%if "%{?tizen_profile_name}" == "tv"
export CFLAGS+=" -DTV"
%else
export CFLAGS+=" -DUNKNOWN_PROFILE"
%endif
%endif
%endif

cmake . \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DMSGPROC_HDS=%{msgproc_hds} \
    -DMSGPROC_CMD=%{msgproc_cmd} \
    -DMSGPROC_PACKAGE=%{msgproc_package} \
    -DMSGPROC_SYSTEM=%{msgproc_system} \
    -DMSGPROC_VCONF=%{msgproc_vconf} \
    -DMSGPROC_SUSPEND=%{msgproc_suspend} \
    -DMSGPROC_LOCATION=%{msgproc_location}

%build

make

%install
rm -rf %{buildroot}

# for systemd
if [ ! -d %{buildroot}/usr/lib/systemd/system/emulator.target.wants ]; then
    mkdir -p %{buildroot}/usr/lib/systemd/system/emulator.target.wants
fi

cp emuld.service %{buildroot}/usr/lib/systemd/system/.
ln -s ../emuld.service %{buildroot}/usr/lib/systemd/system/emulator.target.wants/emuld.service

# for license
mkdir -p %{buildroot}/usr/share/license
cp LICENSE %{buildroot}/usr/share/license/%{name}
cp LICENSE %{buildroot}/usr/share/license/libemuld

%make_install

%clean
make clean
rm -rf CMakeCache.txt
rm -rf CMakeFiles
rm -rf cmake_install.cmake
rm -rf Makefile
rm -rf install_manifest.txt

%post
chmod 770 %{_prefix}/bin/emuld

%files -n emuld
%defattr(-,root,root,-)
%manifest emuld.manifest
%{_prefix}/bin/emuld
/usr/share/license/%{name}
/usr/lib/systemd/system/emuld.service
/usr/lib/systemd/system/emulator.target.wants/emuld.service

%files -n libemuld
%defattr(-,root,root,-)
%manifest libemuld.manifest
/usr/share/license/libemuld
%{_libdir}/libemuld.so.*

%files -n libemuld-devel
%defattr(-,root,root,-)
%{_includedir}/libemuld/*.h
%{_libdir}/libemuld.so
%{_libdir}/pkgconfig/libemuld.pc

%changelog
