#!/bin/sh
echo "Build static webpage"
if [ ! -e node_modules/gulp/bin/gulp.js ]; then
    echo "Installing dependencies"
    npm install --only=dev
fi

mkdir -p src/data
mkdir -p src/static
node node_modules/gulp/bin/gulp.js || exit

platformio run
