Name:       SyncServer
Summary:    File synchronization service
Version:    1.0.0
Release:    1
License:    MIT
Group:      Applications/System
URL:        https://example.org
Source0:    %{name}-%{version}.tar.bz2

BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)

%description
SyncServer is a lightweight synchronization agent for local and remote file sync,
designed to work over TCP/UDP with discovery and push/pull updates. Compatible with Aurora OS.

%prep
%setup -q

%build
qmake
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
install -d %{buildroot}/usr/bin
install -m 755 SyncServer %{buildroot}/usr/bin/SyncServer

# .desktop
install -d %{buildroot}/usr/share/applications
install -m 644 SyncServer.desktop %{buildroot}/usr/share/applications/SyncServer.desktop

# иконка (опционально)
# install -d %{buildroot}/usr/share/icons/hicolor/64x64/apps
# install -m 644 icons/SyncServer.png %{buildroot}/usr/share/icons/hicolor/64x64/apps/SyncServer.png

#desktop-file-install --delete-original         --dir %{buildroot}%{_datadir}/applications                %{buildroot}%{_datadir}/applications/*.desktop

%files
/usr/bin/SyncServer
/usr/share/applications/SyncServer.desktop
# /usr/share/icons/hicolor/64x64/apps/SyncServer.png

