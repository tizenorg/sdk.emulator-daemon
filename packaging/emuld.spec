#git:/slp/pkgs/e/emulator-daemon
Name: emuld
Version: 0.2.31
Release: 1
Summary: emuld is used for communication emulator between and ide.
License: Apache
Source0: %{name}-%{version}.tar.gz
Source1001: packaging/emuld.manifest
BuildRequires: cmake
BuildRequires:  pkgconfig(vconf)

%description

%prep
%setup -q

%build
export LDFLAGS+="-Wl,--rpath=%{_prefix}/lib -Wl,--as-needed"    
    
LDFLAGS="$LDFLAGS" cmake . -DCMAKE_INSTALL_PREFIX=%{_prefix}

make

%install
rm -rf %{buildroot}
if [ ! -d %{buildroot}/usr/lib/systemd/system/multi-user.target.wants ]; then
    mkdir -p %{buildroot}/usr/lib/systemd/system/multi-user.target.wants
fi
cp emuld.service %{buildroot}/usr/lib/systemd/system/.
ln -s ../emuld.service %{buildroot}/usr/lib/systemd/system/multi-user.target.wants/emuld.service

%make_install

%clean
make clean
rm -rf CMakeCache.txt
rm -rf CMakeFiles
rm -rf cmake_install.cmake
rm -rf Makefile
rm -rf install_manifes.txt

%post
chmod 777 /usr/bin/emuld
mkdir -p /opt/nfc
touch /opt/nfc/sdkMsg

%postun

%files
%defattr(-,root,root,-)
%{_prefix}/bin/emuld
/usr/lib/systemd/system/emuld.service
/usr/lib/systemd/system/multi-user.target.wants/emuld.service

%changelog
