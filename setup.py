# This Python script can be used to install the Python 'pynorm' package
import platform
from distutils.core import setup, Extension

# Note to use 'pynorm", you will need to have the libnorm shared library
# (libnorm.so or libnorm.dylib, etc) installed where your Python  installation
# will find it with dlopen() (e.g. /usr/local/lib or something)

setup(name='pynorm', 
      version = '1.0',
      packages = ['pynorm', 'pynorm.extra'],
      package_dir={'' : 'src'})
