FROM ubuntu:latest

# Setup
RUN apt-get install -y --no-install-recommends gtk-doc-tools intltool libaudit-dev libgcrypt20-dev libgirepository1.0-dev libglib2.0-dev libgtk-3-dev libpam0g-dev libqt4-dev libxcb1-dev libxdmcp-dev libxklavier-dev qtbase5-dev valac yelp-tools
RUN ./autogen.sh && make
