# setup.py - Build script for pyapex Python extension

from setuptools import setup, Extension
import os

# Paths relative to this file
here = os.path.dirname(os.path.abspath(__file__))
libapex_src = os.path.join(here, 'src', 'libapex.c')
pyapex_src = os.path.join(here, 'src', 'pyapex.c')
include_dir = os.path.join(here, 'include')

pyapext_module = Extension(
    'pyapex',
    sources=[pyapex_src, libapex_src],
    include_dirs=[include_dir],
    extra_compile_args=[
        '-Wall',
        '-Wextra',
        '-Wno-unused-parameter',
        '-std=c11',
    ],
)

setup(
    name='pyapex',
    version='0.1.0',
    description='Python bindings for GhostBlade hardware (libapex)',
    long_description=open(os.path.join(here, 'README.md')).read() if os.path.exists(os.path.join(here, 'README.md')) else '',
    long_description_content_type='text/markdown',
    author='GhostBlade Project',
    license='GPL-2.0-or-later',
    url='https://github.com/jayis1/ghostblade',
    ext_modules=[pyapext_module],
    python_requires='>=3.8',
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: GNU General Public License v2 or later (GPLv2+)',
        'Operating System :: POSIX :: Linux',
        'Programming Language :: C',
        'Programming Language :: Python :: 3',
        'Topic :: System :: Hardware :: Hardware Drivers',
    ],
)