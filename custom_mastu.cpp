#include "custom_mastu.h"

#include <clientserver/initStructs.h>
#include <clientserver/stringUtils.h>
#include <clientserver/udaStructs.h>
#include <clientserver/udaTypes.h>
#include <ios>
#include <plugins/pluginStructs.h>
#include <plugins/udaPlugin.h>
#include <c++/UDA.hpp>
#include <fmt/format.h>
#include <boost/algorithm/string.hpp>

#include <deque>
#include <fstream>
#include "ext_include/uda_plugin_helpers.hpp"
#include "ext_include/utils/nlohmann/json.hpp"

#include <cmath>
#include <sstream>

class CustomMastuPlugin {
  public:
    void init(IDAM_PLUGIN_INTERFACE* plugin_interface) {
        REQUEST_DATA* request = plugin_interface->request_data;
        if (!init_ || STR_IEQUALS(request->function, "init") || STR_IEQUALS(request->function, "initialise")) {
            reset(plugin_interface);
            // Initialise plugin
            init_ = true;
        }
    }
    void reset(IDAM_PLUGIN_INTERFACE* plugin_interface) {
        if (!init_) {
            // Not previously initialised: Nothing to do!
            return;
        }
        // Free Heap & reset counters
        init_ = false;
    }

    int help(IDAM_PLUGIN_INTERFACE* plugin_interface);
    int version(IDAM_PLUGIN_INTERFACE* plugin_interface);
    int build_date(IDAM_PLUGIN_INTERFACE* plugin_interface);
    int default_method(IDAM_PLUGIN_INTERFACE* plugin_interface);
    int max_interface_version(IDAM_PLUGIN_INTERFACE* plugin_interface);

    int pf_coil_current(IDAM_PLUGIN_INTERFACE* plugin_interface);
    int pf_conn_matrix(IDAM_PLUGIN_INTERFACE* interface);

  private:
    bool init_ = false;
};

// placeholder pf_passive_geometry

int CustomMastuPlugin::pf_coil_current(IDAM_PLUGIN_INTERFACE* interface) {

    //////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////
    DATA_BLOCK* data_block = interface->data_block;
    REQUEST_DATA* request_data = interface->request_data;

    initDataBlock(data_block);
    data_block->rank = 0;
    data_block->dims = nullptr;

    int source{0};
    FIND_REQUIRED_INT_VALUE(request_data->nameValueList, source);
    const char* signal{nullptr};
    FIND_REQUIRED_STRING_VALUE(request_data->nameValueList, signal);
    std::string signal_str{signal};

    std::deque<std::string> split_signal;
    boost::split(split_signal, signal_str, boost::is_any_of("/"));
    int error_code{1};

    std::stringstream request;
    request << "UDA::get(signal=" << signal_str << ",source=" << source << ")";
    const auto request_str = request.str();

    if ( split_signal.back() == "PC" ) {
        std::vector<float> temporary_vector { 0. };
        error_code = imas_json_plugin::uda_helpers::setReturnDataArrayType_Vec(data_block, temporary_vector);
    } else {
        error_code = callPlugin(interface->pluginList, request_str.c_str(), interface);
        if ( split_signal.back() == "P1" ) {
            auto* data = reinterpret_cast<float*>(data_block->data);
            const size_t array_size(data_block->data_n);
            const auto span = gsl::span{data, array_size};
            std::for_each(span.begin(), span.end(), [&](float& elem) { elem *= 0.5; });
            error_code = 0;
        }
    }

    return error_code;
}

int CustomMastuPlugin::pf_conn_matrix(IDAM_PLUGIN_INTERFACE* interface) {
    
    DATA_BLOCK* data_block = interface->data_block;
    REQUEST_DATA* request_data = interface->request_data;

    initDataBlock(data_block);
    data_block->rank = 0;
    data_block->dims = nullptr;

    const char* ps_name{nullptr};
    FIND_REQUIRED_STRING_VALUE(request_data->nameValueList, ps_name);
    std::string ps_name_str{ps_name};

    std::string const map_dir = getenv("UDA_JSON_MAPPING_DIR"); // NOLINT(concurrency-mt-unsafe)
    auto file_path = map_dir + "/mastu/mappings/pf_active/pf_connections.json";
    std::ifstream conn_file;
    conn_file.open(file_path);

    nlohmann::json matrix_json;
    if (conn_file) {
        try {
            conn_file >> matrix_json;
            matrix_json = matrix_json.at(ps_name_str);
        } catch (nlohmann::json::exception& ex) {
            std::string json_error{"CustomMastuPlugin::pf_conn_matrix - "};
            json_error.append(ex.what());
            RAISE_PLUGIN_ERROR(json_error.c_str())
        }
    } else {
        RAISE_PLUGIN_ERROR("CustomMastuPlugin::pf_conn_matrix - Cannot open JSON globals file")
    }
    
    // rows , columns
    std::vector<size_t> shape{matrix_json.size(), matrix_json.front().size()};

    std::vector<int> flat_matrix_vector;
    flat_matrix_vector.reserve(matrix_json.front().size() * matrix_json.size());

    for (int i = 0; i < matrix_json.front().size(); i++) { // columns
        for (int j = 0; j < matrix_json.size(); j++) { // rows
            flat_matrix_vector.push_back(matrix_json[j][i]);
        }
    }
    return setReturnDataIntArray(interface->data_block, flat_matrix_vector.data(), shape.size(), shape.data(), nullptr);
}


int CustomMastu(IDAM_PLUGIN_INTERFACE* plugin_interface) {
    //----------------------------------------------------------------------------------------
    // Standard v1 Plugin Interface

    if (plugin_interface->interfaceVersion > THISPLUGIN_MAX_INTERFACE_VERSION) {
        RAISE_PLUGIN_ERROR("Plugin Interface Version Unknown to this plugin: Unable to execute the request!");
    }

    plugin_interface->pluginVersion = THISPLUGIN_VERSION;
    REQUEST_DATA* request = plugin_interface->request_data;

    //----------------------------------------------------------------------------------------
    // Heap Housekeeping

    // Plugin must maintain a list of open file handles and sockets: loop over and close all files and sockets
    // Plugin must maintain a list of plugin functions called: loop over and reset state and free heap.
    // Plugin must maintain a list of calls to other plugins: loop over and call each plugin with the housekeeping
    // request Plugin must destroy lists at end of housekeeping

    // A plugin only has a single instance on a server. For multiple instances, multiple servers are needed.
    // Plugins can maintain state so recursive calls (on the same server) must respect this.
    // If the housekeeping action is requested, this must be also applied to all plugins called.
    // A list must be maintained to register these plugin calls to manage housekeeping.
    // Calls to plugins must also respect access policy and user authentication policy

    try {
        static CustomMastuPlugin plugin = {};
        auto* const plugin_func = request->function;

        if (plugin_interface->housekeeping || STR_IEQUALS(plugin_func, "reset")) {
            plugin.reset(plugin_interface);
            return 0;
        }

        //----------------------------------------------------------------------------------------
        // Initialise
        plugin.init(plugin_interface);
        if (STR_IEQUALS(plugin_func, "init") || STR_IEQUALS(plugin_func, "initialise")) {
            return 0;
        }

        //----------------------------------------------------------------------------------------
        // Plugin Functions
        //----------------------------------------------------------------------------------------

        //----------------------------------------------------------------------------------------
        // Standard methods: version, builddate, defaultmethod, maxinterfaceversion

        if (STR_IEQUALS(plugin_func, "help")) {
            return plugin.help(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "version")) {
            return plugin.version(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "builddate")) {
            return plugin.build_date(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "defaultmethod")) {
            return plugin.default_method(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "maxinterfaceversion")) {
            return plugin.max_interface_version(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "pf_coil_current")) {
            return plugin.pf_coil_current(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "pf_conn_matrix")) {
            return plugin.pf_conn_matrix(plugin_interface);
        } else {
            RAISE_PLUGIN_ERROR("Unknown function requested!");
        }
    } catch (const std::exception& ex) {
        RAISE_PLUGIN_ERROR(ex.what());
    }
}

/**
 * Help: A Description of library functionality
 * @param interface
 * @return
 */
int CustomMastuPlugin::help(IDAM_PLUGIN_INTERFACE* interface) {
    const char* help = "\ntemplatePlugin: Add Functions Names, Syntax, and Descriptions\n\n";
    const char* desc = "templatePlugin: help = description of this plugin";

    return setReturnDataString(interface->data_block, help, desc);
}

/**
 * Plugin version
 * @param interface
 * @return
 */
int CustomMastuPlugin::version(IDAM_PLUGIN_INTERFACE* interface) {
    return setReturnDataIntScalar(interface->data_block, THISPLUGIN_VERSION, "Plugin version number");
}

/**
 * Plugin Build Date
 * @param interface
 * @return
 */
int CustomMastuPlugin::build_date(IDAM_PLUGIN_INTERFACE* interface) {
    return setReturnDataString(interface->data_block, __DATE__, "Plugin build date");
}

/**
 * Plugin Default Method
 * @param interface
 * @return
 */
int CustomMastuPlugin::default_method(IDAM_PLUGIN_INTERFACE* interface) {
    return setReturnDataString(interface->data_block, THISPLUGIN_DEFAULT_METHOD, "Plugin default method");
}

/**
 * Plugin Maximum Interface Version
 * @param interface
 * @return
 */
int CustomMastuPlugin::max_interface_version(IDAM_PLUGIN_INTERFACE* interface) {
    return setReturnDataIntScalar(interface->data_block, THISPLUGIN_MAX_INTERFACE_VERSION, "Maximum Interface Version");
}
