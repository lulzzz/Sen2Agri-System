#!/bin/bash
#set -x #echo on

##
## SCRIPT: INSTALL SEN2AGRI COMPILATION DEPENDENCIES
##
##
## SCRIPT STEPS
##     - INSTALL PREREQUISITES FOR OTB APP AND OTHER TOOL FOR RPM PACKAGE GENERATION
################################################################################################
###########################CONFIG PART###########################################################
### DEPENDENCIES
: ${PLATFORM_BUILD_EPEL_DEP:="epel-release"}
: ${PLATFORM_BUILD_DEP:="git gcc gcc-c++ cmake boost-devel curl-devel expat-devel fftw-devel gdal-devel geos-devel libgeotiff-devel libjpeg-turbo-devel libsvm-devel muParser-devel opencv-devel openjpeg2-devel openjpeg2-tools pcre-devel libpng-devel proj-devel proj-epsg python-devel qt-devel sqlite-devel swig libtiff-devel tinyxml-devel qt5-qtbase-devel qt5-qtbase-postgresql gsl-devel qt-x11"}
#-----------------------------------------------------------#
function install_PLATFORM_DEP_package()
{
   yum -y install ${PLATFORM_BUILD_EPEL_DEP}
   yum -y install ${PLATFORM_BUILD_DEP}
}
#-----------------------------------------------------------#
function install_FPMTool_package()
{
   yum -y install rpm-build ruby-devel build-essential rubygems

   gem install fpm
}
#-----------------------------------------------------------#
function prerequisitesInstall()
{
   install_PLATFORM_DEP_package

   install_FPMTool_package
}
#-----------------------------------------------------------#
###########################################################
#####  MAIN                                          ######
###########################################################
##install additional packages
prerequisitesInstall
