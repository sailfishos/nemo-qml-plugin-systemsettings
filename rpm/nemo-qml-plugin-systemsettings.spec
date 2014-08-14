Name:       nemo-qml-plugin-systemsettings
Summary:    System settings plugin for Nemo Mobile
Version:    0.0.25
Release:    1
Group:      System/Libraries
License:    BSD
URL:        https://github.com/nemomobile/nemo-qml-plugin-systemsettings
Source0:    %{name}-%{version}.tar.bz2
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5SystemInfo)
BuildRequires:  pkgconfig(qmsystem2-qt5)  >= 1.4.17
BuildRequires:  pkgconfig(timed-qt5)
BuildRequires:  pkgconfig(profile)
BuildRequires:  pkgconfig(mce)

%description
%{summary}.

%package devel
Summary:    System settings C++ library
Group:      System/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
%{summary}.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 
make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%qmake5_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/qt5/qml/org/nemomobile/systemsettings/libnemosystemsettings.so
%{_libdir}/qt5/qml/org/nemomobile/systemsettings/qmldir
%{_libdir}/libsystemsettings.so.*

%files devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/systemsettings.pc
%{_includedir}/systemsettings/*
%{_libdir}/libsystemsettings.so
