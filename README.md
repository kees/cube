cube
====

simple directory tree based media player

configuration
-------------
- ~/.config/Outflux/playback-walker.conf

cache
-----
- ~/.cache/playback/

dependencies
------------
Run
- apt install mpv mediainfo
- apt install qt6-qpa-plugins libqt6widgets6 libqt6concurrent6 libqt6gui6

Build
- apt install qtcreator qt6-base-dev qt6-base-dev-tools qmake6
		qml-qt6 qt6-qmltooling-plugins

deployment
----------
- release build
	export LANG=C.UTF-8
	mkdir -p build
	qmake6 -o build/Makefile cube.pro
	make -C build
