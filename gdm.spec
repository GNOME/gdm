# Note that this is NOT a relocatable package
%define ver     1.0.1
%define rel     1
%define prefix  /usr

Summary: GNOME Display Manager
Name: gdm
Version: %ver
Release: %rel
Copyright: GPL
Group: User Interface/X
Source: ftp://ftp.gnome.org/sources/gdm/gdm-%{ver}.tar.gz

BuildRoot: /var/tmp/gdm-%{PACKAGE_VERSION}-root
Docdir: %{prefix}/doc
Requires: gnome-libs >= 1.0.0

%description 
GNOME Display Manager allows you to log into your system with the
X Window System running.  It is highly configurable, allowing you
to run several different X sessions at once on your local machine,
and can manage login connections from remote machines as well.

%changelog

%prep
%setup

%build
libtoolize --copy --force
CFLAGS="$RPM_OPT_FLAGS" ./configure --prefix=%prefix --sysconfdir=/etc --localstatedir=/var
make

%install
rm -rf $RPM_BUILD_ROOT

/usr/sbin/useradd -u 42 -r gdm > /dev/null 2>&1 || /bin/true

make prefix=$RPM_BUILD_ROOT%{prefix} sysconfdir=$RPM_BUILD_ROOT/etc localstatedir=$RPM_BUILD_ROOT/var install
# docs go elsewhere
rm -rf $RPM_BUILD_ROOT/%{prefix}/doc

%clean
[ -n "$RPM_BUILD_ROOT" -a "$RPM_BUILD_ROOT" != / ] && rm -rf $RPM_BUILD_ROOT

%pre
/usr/sbin/useradd -u 42 -r gdm > /dev/null 2>&1
# ignore errors, as we can't disambiguate between gdm already existed
# and couldn't create account with the current adduser.
exit 0

%files
%defattr(-, root, root)

%doc AUTHORS COPYING ChangeLog NEWS README README.install docs/gdm-manual.txt
%{prefix}/bin/*
%config /etc/pam.d/gdm
%config /etc/gnomerc
%config /etc/gdm/gdm.conf
%config /etc/gdm/Sessions/*
%{prefix}/share/locale/*/*/*
%{prefix}/share/pixmaps/*
%attr(750, gdm, gdm) %dir /var/gdm

%changelog
* Thu Mar 4 1999 Martin K. Petersen <mkp@mkp.net>
- misc. fixes. Red Hatters: I removed your gdm.conf/Xsession patch
  from this spec file. Stuff it back in for your own builds.

* Thu Feb 25 1999 Michael Fulbright <drmike@redhat.com>
- moved files from /usr/etc to /etc

* Tue Feb 16 1999 Michael Johnson <johnsonm@redhat.com>
- removed commented-out #1 definition -- put back after testing gnome-libs
  comment patch

* Sat Feb 06 1999 Michael Johnson <johnsonm@redhat.com>
- initial packaging
