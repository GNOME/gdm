#!/usr/bin/env python3

import os;

pam_configs = [('redhat', 'redhat'),
               ('fedora', 'redhat'),
               ('exherbo', 'exherbo'),
               ('arch', 'arch'),
               ('lfs', 'lfs')]

for distro, pam_config in pam_configs:
    release_file = os.path.join('/', 'etc', '{}-release'.format(distro))
    if os.path.exists(release_file):
        print(pam_config)
        exit(0)

print('none')
