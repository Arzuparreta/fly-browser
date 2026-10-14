const fs = require('node:fs');
const path = require('node:path');

// Launch a real application package. Passing a loose .cjs file makes Fly treat
// its bootstrap as a URL argument and exposes Electron's version as the app version.
module.exports = function createElectronFixture(root, directory) {
    fs.writeFileSync(path.join(directory, 'package.json'), JSON.stringify({
        name: 'fly-desktop-fixture',
        version: require(path.join(root, 'package.json')).version,
        main: 'main.js'
    }));
    fs.writeFileSync(path.join(directory, 'main.js'), `const {app}=require('electron'); app.setAppPath(${JSON.stringify(root)}); app.setPath('home',${JSON.stringify(directory)}); app.setPath('userData',${JSON.stringify(path.join(directory, 'data'))}); require(${JSON.stringify(path.join(root, 'main.js'))});`);
    return directory;
};
