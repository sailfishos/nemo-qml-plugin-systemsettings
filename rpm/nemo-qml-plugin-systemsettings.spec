Name:       nemo-qml-plugin-systemsettings
Summary:    System settings plugin for Nemo Mobile
Version:    0.10.1
Release:    1
License:    BSD
URL:        https://github.com/sailfishos/nemo-qml-plugin-systemsettings/
Source0:    %{name}-%{version}.tar.bz2
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
Requires:       connman
Requires:       mce >= 1.113.0
Requires:       libsailfishkeyprovider >= 0.0.14
Requires:       connman-qt5 >= 1.2.48
Requires:       user-managerd >= 0.4.0
Requires:       udisks2 >= 2.8.1+git6
Requires:       mlite-qt5 >= 0.3.6
Requires(post): coreutils
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(timed-qt5)
BuildRequires:  pkgconfig(profile)
BuildRequires:  pkgconfig(mce) >= 1.32.0
BuildRequires:  pkgconfig(mlite5) >= 0.3.6
BuildRequires:  pkgconfig(usb-moded-qt5)
BuildRequires:  pkgconfig(blkid)
BuildRequires:  pkgconfig(libcrypto)
BuildRequires:  pkgconfig(nemodbus) >= 2.1.16
BuildRequires:  pkgconfig(libsailfishkeyprovider) >= 0.0.14
BuildRequires:  pkgconfig(connman-qt5) >= 1.2.48
BuildRequires:  pkgconfig(ssu-sysinfo) >= 1.4.0
BuildRequires:  pkgconfig(packagekitqt5)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(sailfishaccesscontrol)
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(sailfishusermanager)
BuildRequires:  qt5-qttools-linguist
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(qofono-qt5) >= 0.105

%description
%{summary}.

%package devel
Summary:    System settings C++ library
Requires:   %{name} = %{version}-%{release}

%description devel
%{summary}.

%package ts-devel
Summary: Translation source for %{name}

%description ts-devel
%{summary}.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}

%install
%qmake5_install

%post
/sbin/ldconfig
# Note: can be removed after a stop release
if [ ! -e /var/lib/location/configuration-migrated ]
then
    cp /etc/location/location.conf /var/lib/location/location.conf || :
    touch /var/lib/location/configuration-migrated || :
fi

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%license LICENSE.BSD
%{_libdir}/qt5/qml/org/nemomobile/systemsettings/libnemosystemsettings.so
%{_libdir}/qt5/qml/org/nemomobile/systemsettings/plugins.qmltypes
%{_libdir}/qt5/qml/org/nemomobile/systemsettings/qmldir
%{_libdir}/libsystemsettings.so.*
%attr(4710,-,privileged) %{_libexecdir}/setlocale
%dir %attr(0775, root, privileged) /etc/location
%config %attr(0664, root, privileged) /etc/location/location.conf
%dir %attr(0775, root, privileged) /var/lib/location
%config %attr(0664, root, privileged) /var/lib/location/location.conf
%{_datadir}/translations/*.qm

%files devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/systemsettings.pc
%{_includedir}/systemsettings/*
%{_libdir}/libsystemsettings.so

%files ts-devel
%{_datadir}/translations/source/*.ts
