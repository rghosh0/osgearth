from conans import ConanFile, CMake, tools
import os

def get_version():
    defaultFullVersion='2.10.2-0-00000'
    defaultVersion='2.10.2_branch'
    return (defaultFullVersion, defaultVersion)

class OsgEarthConan(ConanFile):
    name = "OsgEarth"
    (version_full, version) = get_version()
    license = "https://github.com/gwaldron/osgearth/blob/master/LICENSE.txt"
    description = "Forked OsgEarth dependancy"
    url = "https://github.com/MPcoreDev/osgearth"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "qt_version": "ANY"}
    #default_options = {"shared": False, "qt_version": "5.12.4"}
    #default_options = {"shared": False, "boost:header_only": True}
    generators = ["cmake"]
    short_paths = True #enable short path for windows builds
    exports = ["CMakeLists.txt"]
    exports_sources = ["CMakeLists.txt", "src*", "include*", "tests*", "CMakeModules*", "docs*", "LICENSE.txt"] #List only files necessary to build the package
    keep_imports = True #dlls copied by the imports function should stay in the install folder
    qt_install_prefix = "" #stores the Qt install directory because other deps are there too
    #the following class variables from ConanFile may be undefined, so make sure they are empty strings in that case
    install_folder = ""
    source_folder = ""
    
    def requirements(self):
        #self.requires.add("boost/[1.69]@conan/stable") #link with boost geometry
        #self.requires.add("geos/3.6.3@conan/stable")
        #self.requires.add("gdal/2.2.4@conan/stable")
        self.requires.add("OpenSceneGraph/3.6.4b@conan/stable")#geos and gdal included by OpenSceneGraph
        self.requires.add("ConanSharedFunctions/0.0.1@navblue/stable")

    def imports(self):
        from SharedFunctions import mission_plus_imports
        mission_plus_imports(self)

    def build(self):
        # Set up CMake and its variables
        myCMake = CMake(self)
        myCMake.verbose = True

        # Copy CMakeModules to osgearth
        myCMake.definitions["CMAKE_MODULE_PATH"] = os.path.join(self.source_folder, "CMakeModules").replace("\\", "/")
    
        # Force CMAKE_BUILD_TYPE to be defined so that configure steps depending on it will correctly be executed
        myCMake.definitions["CMAKE_BUILD_TYPE"] = self.settings.build_type
    
        # GDAL dir is necessary for osgearth, traditional find does not work properly on windows
        myCMake.definitions["GDAL_DIR"] = self.deps_cpp_info["gdal"].rootpath
        print (" * OsgEarth GDAL DIR: ", myCMake.definitions["GDAL_DIR"])
    
        # Other options
        myCMake.definitions["BUILD_APPLICATIONS"] = False
        myCMake.definitions["BUILD_OSGEARTH_EXAMPLES"] = False
        myCMake.definitions["BUILD_TESTS"] = False
    
        # Add the crosscompilation toolchain file for iOS target
        if self.settings.os == "iOS":
            if self.settings.arch == "x86_64":
                myCMake.definitions["CMAKE_TOOLCHAIN_FILE"] = mptPrefix + "/MissionPlusTools/cmake/iOS/ios.simulator.toolchain.cmake"
            else:
                myCMake.definitions["CMAKE_TOOLCHAIN_FILE"] = mptPrefix + "/MissionPlusTools/cmake/iOS/ios.toolchain.cmake"

            myCMake.generator = "Xcode"
            # Setup the application profile used to configure iOS application bundle if ios_app_profile is set
            if 'ios_app_profile' in self.options and self.options.ios_app_profile:
                # ... and cmake will use a file named ios_app_profile.cmake to configure the application signature mechanism
                # and all other xcode related information (app. icon ...)
                myCMake.definitions["MP_IOS_APP_PROFILE"] = self.options.ios_app_profile

        # Call the configure function
        myCMake.configure()

        # ... and build
        myCMake.build()

    def package(self):
        from SharedFunctions import mission_plus_package
        conanfile_folder = os.path.dirname(os.path.realpath(__file__))
        mission_plus_package(self, conanfile_folder)
        
    def package_info(self):
        from SharedFunctions import mission_plus_package_info
        mission_plus_package_info(self)

    def deploy(self):
        from SharedFunctions import mission_plus_deploy
        mission_plus_deploy(self)

        self.copy("*.lib", dst="lib", src="lib")
        self.copy("*", dst="include", src="include")
        self.copy("*.dll", dst="bin", src="bin")
        self.copy("*.pdb", dst="bin", src="bin")
        self.copy_deps("*.lib", dst="lib", src="lib")
        self.copy_deps("*", dst="include", src="include")
        self.copy_deps("*.dll", dst="bin", src="bin")
        self.copy_deps("*.pdb", dst="bin", src="bin") 
