Name:       nemo-qml-plugin-systemsettings
Summary:    System settings plugin for Nemo Mobile
Version:    0.5.37
Release:    1
Group:      System/Libraries
License:    BSD
URL:        https://git.sailfishos.org/mer-core/nemo-qml-plugin-systemsettings
Source0:    %{name}-%{version}.tar.bz2
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
Requires:       connman
Requires:       mce >= 1.83.0
Requires:       libsailfishkeyprovider >= 0.0.14
Requires:       connman-qt5 >= 1.2.21
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5SystemInfo)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  pkgconfig(timed-qt5)
BuildRequires:  pkgconfig(profile)
BuildRequires:  pkgconfig(mce) >= 1.21.0
BuildRequires:  pkgconfig(mlite5)
BuildRequires:  pkgconfig(usb-moded-qt5)
BuildRequires:  pkgconfig(libshadowutils)
BuildRequires:  pkgconfig(blkid)
BuildRequires:  pkgconfig(libcrypto)
BuildRequires:  pkgconfig(nemodbus) >= 2.1.16
BuildRequires:  pkgconfig(nemomodels-qt5)
BuildRequires:  pkgconfig(libsailfishkeyprovider) >= 0.0.14
BuildRequires:  pkgconfig(connman-qt5) >= 1.2.23
BuildRequires:  pkgconfig(ssu-sysinfo) >= 1.1.0
BuildRequires:  pkgconfig(packagekitqt5)
BuildRequires:  pkgconfig(glib-2.0)

%description
%{summary}.

%package devel
Summary:    System settings C++ library
Group:      System/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
%{summary}.

%package tests
Summary:    System settings C++ library (unit tests)
Group:      System/Libraries

%description tests
%{summary}.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/qt5/qml/org/nemomobile/systemsettings/libnemosystemsettings.so
%{_libdir}/qt5/qml/org/nemomobile/systemsettings/plugins.qmltypes
%{_libdir}/qt5/qml/org/nemomobile/systemsettings/qmldir
%{_libdir}/libsystemsettings.so.*
%attr(4710,-,privileged) %{_libexecdir}/setlocale
%dir %attr(0775, root, privileged) /etc/location
%config %attr(0664, root, privileged) /etc/location/location.conf

%files devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/systemsettings.pc
%{_includedir}/systemsettings/*
%{_libdir}/libsystemsettings.so

%files tests
%defattr(-,root,root,-)
%{_libdir}/%{name}-tests/ut_diskusage
%{_datadir}/%{name}-tests/tests.xml
