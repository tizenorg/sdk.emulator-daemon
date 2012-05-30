#git:/slp/pkgs/e/emulator-daemon
Name: emuld
Version: 0.2.5
Release: 1
Summary: emuld is used for communication emulator between and ide.
License: Apache
Source0: %{name}-%{version}.tar.gz
Source1001: packaging/emuld.manifest 
BuildArch: i386
ExclusiveArch: %{ix86}
BuildRequires: cmake

%description

%prep
%setup -q

%build
cp %{SOURCE1001} .
export LDFLAGS+="-Wl,--rpath=%{_prefix}/lib -Wl,--as-needed"    
    
LDFLAGS="$LDFLAGS" cmake . -DCMAKE_INSTALL_PREFIX=%{_prefix}

make

%install
rm -rf %{buildroot}
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

%postun

%files
%manifest emuld.manifest
%defattr(-,root,root,-)
%{_prefix}/bin/emuld

%changelog
