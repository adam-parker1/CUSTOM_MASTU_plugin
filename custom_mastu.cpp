#include "custom_mastu.h"

#include <deque>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <optional>
#include <functional>

#include <boost/algorithm/string.hpp>
#include <fmt/format.h>

// UDA includes
#include <c++/UDA.hpp>
#include <clientserver/initStructs.h>
#include <clientserver/stringUtils.h>
#include <clientserver/udaStructs.h>
#include <clientserver/udaTypes.h>
#include <plugins/pluginStructs.h>
#include <plugins/udaPlugin.h>

#include "ext_include/nlohmann/json.hpp"
#include "utils/uda_plugin_helpers.hpp"

class CustomMastuPlugin {
  public:
    void init(IDAM_PLUGIN_INTERFACE* plugin_interface) {
        REQUEST_DATA* request = plugin_interface->request_data;
        if (!init_ || STR_IEQUALS(request->function, "init") || STR_IEQUALS(request->function, "initialise")) {
            reset(plugin_interface);
            // Initialise plugin
            const char* cache = std::getenv("UDA_CUSTOM_MASTU_PLUGIN_CLIENT_CACHE");
            cache_enabled_ = (cache == nullptr) or (std::stoi(cache) > 0);
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

    int custom_passive_structures(IDAM_PLUGIN_INTERFACE* interface);
    int pf_coil_current(IDAM_PLUGIN_INTERFACE* plugin_interface);
    int pf_conn_matrix(IDAM_PLUGIN_INTERFACE* interface);

  private:
    [[nodiscard]] std::string make_cache_key(std::string_view signal, std::string_view source, std::string_view host, int port) const;
    [[nodiscard]] std::optional<std::reference_wrapper<const uda::Result>> check_cache(const std::string& key) const;
     std::optional<std::reference_wrapper<const uda::Result>>get_data(std::string_view signal, std::string_view source, std::string_view host, int port);

    bool init_ = false;
    std::unordered_map<std::string, const uda::Result&> cache_;
    bool cache_enabled_ = true;

};

// placeholder pf_passive_geometry
std::deque<std::string> split_request(std::string_view var) {
    std::deque<std::string> split_vec;
    boost::split(split_vec, var, boost::is_any_of("."));
    return split_vec;
}

int tree_check(uda::TreeNode& temp_tree) {

    if (!temp_tree.numChildren()) {
        UDA_LOG(UDA_LOG_DEBUG, "\nimas_json_plugin::plugin_helpers::tree_check: No children found\n");
        return 1;
    }
    if (temp_tree.child(0).name() != "data") {
        UDA_LOG(UDA_LOG_DEBUG, "\nimas_json_plugin::plugin_helpers::tree_check: No child named data\n");
        return 1;
    }
    return 0;
};

std::vector<std::string> get_treenode_child_names(uda::TreeNode& tree) {
    std::vector<std::string> temp_name_vec;
    for (auto& child : tree.children()) {
        temp_name_vec.push_back(child.name());
    }
    return temp_name_vec;
}

int tree_node_traversal(uda::TreeNode& tree, std::deque<std::string>& vec_split) {

    if (vec_split.size() <= 1 or !tree.numChildren()) {
        UDA_LOG(UDA_LOG_DEBUG, "\nimas_json_plugin::plugin_helpers::tree_node_traversal: TreeNode at bottom level\n");
        return 0;
    }

    std::vector<std::string> children{get_treenode_child_names(tree)};
    auto result = std::find(children.begin(), children.end(), vec_split.front());
    if (result != children.end()) {
        tree = tree.child(std::distance(children.begin(), result));
        vec_split.pop_front();
        tree_node_traversal(tree, vec_split);
    } else {
        UDA_LOG(UDA_LOG_DEBUG, "\nimas_json_plugin::plugin_helpers::tree_node_traversal: Child name not found\n");
        return 1;
    }
    return 0;
};

int set_return_data(IDAM_PLUGIN_INTERFACE* interface, uda::TreeNode& final_tree, const std::string& final_var) {

    std::vector<std::string> anames = final_tree.atomicNames();
    std::vector<std::string> atypes = final_tree.atomicTypes();
    std::vector<bool> apoint = final_tree.atomicPointers();
    std::vector<size_t> arank = final_tree.atomicRank();
    std::vector<std::vector<size_t>> ashape = final_tree.atomicShape();

    auto result = std::find(anames.begin(), anames.end(), final_var);
    if (result == anames.end()) {
        return 1;
    }

    long idx{std::distance(anames.begin(), result)};

    // Would be good to use apoint here but seems to be false every time
    if (arank[idx] > 0) {
        if (atypes[idx] == std::string("int")) {
            imas_json_plugin::uda_helpers::setReturnDataArrayType<int>(
                interface->data_block,
                gsl::span<const int>{static_cast<int*>(final_tree.structureComponentData(final_var)), ashape[idx][0]},
                gsl::span<const size_t>{ashape[idx]});
        } else if (atypes[idx] == std::string("float")) {
            imas_json_plugin::uda_helpers::setReturnDataArrayType<float>(
                interface->data_block,
                gsl::span<const float>{static_cast<float*>(final_tree.structureComponentData(final_var)),
                                       ashape[idx][0]},
                gsl::span<const size_t>{ashape[idx]});
        } else if (atypes[idx] == std::string("double")) {
            imas_json_plugin::uda_helpers::setReturnDataArrayType<double>(
                interface->data_block,
                gsl::span<const double>{static_cast<double*>(final_tree.structureComponentData(final_var)),
                                        ashape[idx][0]},
                gsl::span<const size_t>{ashape[idx]});
        } else {
            UDA_LOG(UDA_LOG_DEBUG, "\nimas_json_plugin::plugin_helpers::set_return_data: Unrecognised data type\n");
            return 1;
        }
    } else {
        if (atypes[idx] == std::string("int")) {
            imas_json_plugin::uda_helpers::setReturnDataScalarType<int>(
                interface->data_block, *static_cast<int*>(final_tree.structureComponentData(final_var)));
        } else if (atypes[idx] == std::string("float")) {
            imas_json_plugin::uda_helpers::setReturnDataScalarType<float>(
                interface->data_block, *static_cast<float*>(final_tree.structureComponentData(final_var)));
        } else if (atypes[idx] == std::string("double")) {
            imas_json_plugin::uda_helpers::setReturnDataScalarType<double>(
                interface->data_block, *static_cast<double*>(final_tree.structureComponentData(final_var)));
        } else {
            UDA_LOG(UDA_LOG_DEBUG, "\nimas_json_plugin::plugin_helpers::set_return_data: Unrecognised data type\n");
            return 1;
        }
    }

    return 0;
};

float get_float_var(uda::TreeNode& final_tree, std::string_view var_str, int element) {

    std::vector<std::string> anames = final_tree.atomicNames();
    std::vector<std::string> atypes = final_tree.atomicTypes();
    std::vector<bool> apoint = final_tree.atomicPointers();
    std::vector<size_t> arank = final_tree.atomicRank();
    std::vector<std::vector<size_t>> ashape = final_tree.atomicShape();

    auto result = std::find(anames.begin(), anames.end(), var_str);
    if (result == anames.end()) {
        // return false;
        throw std::runtime_error("ADAM PUT AN ERROR MESSAGE HERE");
    }

    long idx{std::distance(anames.begin(), result)};

    if (arank[idx] != 1) {
        // return 999.;
        throw std::runtime_error("ADAM PUT AN ERROR MESSAGE HERE");
    }

    // always a float for this case, don't panic
    gsl::span<const float> var_span{static_cast<float*>(final_tree.structureComponentData(std::string{var_str})),
                                    ashape[idx][0]};

    return var_span[element];
}

bool is_rectangular(uda::TreeNode& final_tree, int element) {

    const auto angle1 = get_float_var(final_tree, "shapeAngle1", element);
    const auto angle2 = get_float_var(final_tree, "shapeAngle2", element);

    if (angle1 == 999. or angle2 == 999.) {
        RAISE_PLUGIN_ERROR("PF_PASSIVE ANGLES NOT GOOD");
    }

    bool is_rectangle = (angle1 == 0. and angle2 == 0.);

    return is_rectangle;
}

int handle_rectangle(IDAM_PLUGIN_INTERFACE* interface, uda::TreeNode& final_tree, std::string_view final_var,
                     int element) {

    std::vector<std::string> vecOfStrs{"centreR", "centreZ", "dR", "dZ"};
    if (std::find(vecOfStrs.begin(), vecOfStrs.end(), final_var) == vecOfStrs.end()) {
        return 1;
    }

    float output_float = get_float_var(final_tree, final_var, element);
    if (final_var == "dR" or final_var == "dZ") {
        output_float = std::abs(output_float);
    }

    return imas_json_plugin::uda_helpers::setReturnDataScalarType<float>(interface->data_block,
                                                                         output_float);
}

int handle_oblique(IDAM_PLUGIN_INTERFACE* interface, uda::TreeNode& final_tree, std::string_view final_var,
                   int element) {

    int return_code{1};
    std::vector<std::string> vecOfStrs{"centreR", "centreZ", "dR", "dZ", "shapeAngle1", "shapeAngle2"};
    if (std::find(vecOfStrs.begin(), vecOfStrs.end(), final_var) == vecOfStrs.end()) {
        return return_code;
    }

    const auto temp_var = get_float_var(final_tree, final_var, element);
    const auto deg2rad = M_PI / 180.0;

    if (final_var == "centreR") {
        // lower left corner - r
        const auto temp_angle2 = get_float_var(final_tree, "shapeAngle2", element);
        const auto temp_dR = get_float_var(final_tree, "dR", element);
        const auto temp_dZ = get_float_var(final_tree, "dZ", element);
        float atan2 = 0.;
        if ( temp_angle2 > 0. ) atan2 = 1 / tan(temp_angle2 * deg2rad);
        return_code = imas_json_plugin::uda_helpers::setReturnDataScalarType<float>(
            interface->data_block, temp_var - (temp_dR / 2.0) - (temp_dZ / 2.0) * atan2);
    } else if (final_var == "centreZ") {
        // lower left corner - z
        const auto temp_angle1 = get_float_var(final_tree, "shapeAngle1", element);
        const auto temp_dR = get_float_var(final_tree, "dR", element);
        const auto temp_dZ = get_float_var(final_tree, "dZ", element);
        return_code = imas_json_plugin::uda_helpers::setReturnDataScalarType<float>(
            interface->data_block, temp_var - (temp_dZ / 2.0) - (temp_dR / 2.0) * tan(temp_angle1 * deg2rad));
    } else if (final_var == "dR") {
        // length_alpha
        const auto temp_angle1 = get_float_var(final_tree, "shapeAngle1", element);
        return_code = imas_json_plugin::uda_helpers::setReturnDataScalarType<float>(
            interface->data_block, temp_var / cos(temp_angle1 * deg2rad));
    } else if (final_var == "dZ") {
        // length_beta
        auto temp_angle2 = get_float_var(final_tree, "shapeAngle2", element);
        if (temp_angle2 == 0.) temp_angle2 = 90.;
        return_code = imas_json_plugin::uda_helpers::setReturnDataScalarType<float>(
            interface->data_block, temp_var / sin(temp_angle2 * deg2rad));
    } else if (final_var == "shapeAngle1") {
        // alpha
        return_code =
            imas_json_plugin::uda_helpers::setReturnDataScalarType<float>(interface->data_block, temp_var * deg2rad);
    } else if (final_var == "shapeAngle2") {
        // beta
        return_code = imas_json_plugin::uda_helpers::setReturnDataScalarType<float>(interface->data_block,
                                                                                    (temp_var - 90.0) * deg2rad);
    }

    return return_code;
}

int custom_passive(IDAM_PLUGIN_INTERFACE* interface, uda::TreeNode& final_tree,
                   const std::deque<std::string>& var_stack, int element) {

    if (element == 999 || var_stack.size() != 2) {
        return 1;
    }

    bool is_rectangle = is_rectangular(final_tree, element);
    int return_code{1};

    if (var_stack.front() == "rectangle") {
        // check angles
        // handle rectangle
        return_code = is_rectangle ? handle_rectangle(interface, final_tree, var_stack.back(), element) : 1;
    } else if (var_stack.front() == "oblique") {
        // check angles
        // handle oblique
        return_code = is_rectangle ? 1 : handle_oblique(interface, final_tree, var_stack.back(), element);
    }
    return return_code;
}

std::string CustomMastuPlugin::make_cache_key(std::string_view signal, std::string_view source, std::string_view host, int port) const
{
    if (!cache_enabled_) {
        return {};
    }
    // start with signal as most likely to change most frequently
    return fmt::format("{}|{}|{}|{}", signal, source, host, port);
}

std::optional<std::reference_wrapper<const uda::Result>> CustomMastuPlugin::check_cache(const std::string& key) const
{
    if (!cache_enabled_) {
        return std::nullopt;
    }

    auto result = cache_.find(key);
    if (result != cache_.end()) {
        return std::cref(result->second);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const uda::Result>> CustomMastuPlugin::get_data(std::string_view signal, std::string_view source, std::string_view host, int port) 
{
    static uda::Client client;

    auto cache_key = make_cache_key(signal, source, host, port);
    auto maybe_result = check_cache(cache_key);
    if (maybe_result.has_value()){
        return maybe_result;
    }

    uda::Client::setServerHostName(host.data());
    uda::Client::setServerPort(port);

    const uda::Result& data = client.get(signal.data(), source.data());
    // Check for errors
    if (data.errorCode() != uda::OK) {
        // RAISE_PLUGIN_ERROR("uda::Result data is not uda::OK");
        throw std::runtime_error("uda::Result data is not uda::OK");
    }

    return std::cref(data);
}

int CustomMastuPlugin::custom_passive_structures(IDAM_PLUGIN_INTERFACE* interface) {

    //////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////
    DATA_BLOCK* data_block = interface->data_block;
    REQUEST_DATA* request_data = interface->request_data;

    initDataBlock(data_block);
    data_block->rank = 0;
    data_block->dims = nullptr;

    // TODO: put into plugin relevant structure
    int port{0};
    FIND_REQUIRED_INT_VALUE(request_data->nameValueList, port);
    const char* host{nullptr};
    FIND_REQUIRED_STRING_VALUE(request_data->nameValueList, host);
    std::string const host_str{host};

    int source{0};
    FIND_REQUIRED_INT_VALUE(request_data->nameValueList, source);
    const char* signal{nullptr};
    FIND_REQUIRED_STRING_VALUE(request_data->nameValueList, signal);
    std::string signal_str{signal};
    const char* key{nullptr};
    FIND_REQUIRED_STRING_VALUE(request_data->nameValueList, key);
    std::string const key_str{key};

    int element{999};
    FIND_INT_VALUE(request_data->nameValueList, element);

    std::transform(signal_str.begin(), signal_str.end(), signal_str.begin(), ::tolower);
    std::string geom_request = fmt::format("GEOM::get(signal={}, Config=1)", signal_str);

    auto maybe_result = get_data(geom_request, std::to_string(source), host, port); //throws

    const uda::Result& data = maybe_result->get();
    if (!data.isTree()) {
        RAISE_PLUGIN_ERROR("Returned data is not of expected tree structure");
    }

    std::deque<std::string> split_vec{split_request(key_str)};

    uda::TreeNode root_tree = data.tree();
    // Hack to skip two levels
    if (!static_cast<bool>(tree_check(root_tree))) {
        root_tree = root_tree.child(0);
    }
    if (!static_cast<bool>(tree_check(root_tree))) {
        root_tree = root_tree.child(0);
    }

    if (split_vec.back() == "geometry_type") {
        short temp_type = is_rectangular(root_tree, element) ? 2 : 3;
        return setReturnDataShortScalar(data_block, temp_type, nullptr);
    }
    return custom_passive(interface, root_tree, split_vec, element);
}

int CustomMastuPlugin::pf_coil_current(IDAM_PLUGIN_INTERFACE* interface) {

    //////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////
    REQUEST_DATA* request_data = interface->request_data;
    int port{0};
    FIND_REQUIRED_INT_VALUE(request_data->nameValueList, port);
    const char* host{nullptr};
    FIND_REQUIRED_STRING_VALUE(request_data->nameValueList, host);
    int source{0};
    FIND_REQUIRED_INT_VALUE(request_data->nameValueList, source);
    const char* signal{nullptr};
    FIND_REQUIRED_STRING_VALUE(request_data->nameValueList, signal);
    std::string signal_str{signal};

    std::deque<std::string> split_signal;
    boost::split(split_signal, signal_str, boost::is_any_of("/"));
    if (split_signal.back() == "PC") {
        std::vector<float> temporary_vector{0.};
        return imas_json_plugin::uda_helpers::setReturnDataArrayType_Vec(interface->data_block, temporary_vector);
    }

    int error_code{1};

    auto maybe_result = get_data(signal, std::to_string(source), host, port); //throws
    if (!maybe_result.has_value()) {
       throw std::runtime_error("Error retrieving data in pf_coil_current function");
    }
    const uda::Result& data = maybe_result->get();

    std::vector<int> shape;
    for (const auto& i: data.shape()) {
        shape.emplace_back(static_cast<int>(i));
    }

    DATA_BLOCK* data_block = interface->data_block;
    const char* raw_data = data.raw_data();
    error_code = setReturnData(data_block, const_cast<void*>(reinterpret_cast<const void*>(raw_data)), data.size(), 
            (UDA_TYPE)data.uda_type(), static_cast<int>(data.rank()), shape.data(), nullptr);

    // error_code = callPlugin(interface->pluginList, request_str.c_str(), interface);
    if (split_signal.back() == "P1") {
        auto* data = reinterpret_cast<float*>(data_block->data);
        const size_t array_size(data_block->data_n);
        const auto span = gsl::span{data, array_size};
        std::for_each(span.begin(), span.end(), [&](float& elem) { elem *= 0.5; });
        error_code = 0;
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
    auto file_path = map_dir + "/mastu/pf_active/pf_connections.json";
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
        for (int j = 0; j < matrix_json.size(); j++) {     // rows
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

        int err {0};
        if (STR_IEQUALS(plugin_func, "help")) {
            err = plugin.help(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "version")) {
            err = plugin.version(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "builddate")) {
            err = plugin.build_date(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "defaultmethod")) {
            err = plugin.default_method(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "maxinterfaceversion")) {
            err = plugin.max_interface_version(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "custom_passive_structures")) {
            err = plugin.custom_passive_structures(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "pf_coil_current")) {
            err = plugin.pf_coil_current(plugin_interface);
        } else if (STR_IEQUALS(plugin_func, "pf_conn_matrix")) {
            err = plugin.pf_conn_matrix(plugin_interface);
        } else {
            RAISE_PLUGIN_ERROR_AND_EXIT("Unknown function requested!", plugin_interface);
        }
        if (err != 0) {
            RAISE_PLUGIN_ERROR_AND_EXIT("Bad error status returned from CUSTOM_MASTU plugin", plugin_interface);
        }
        return err;
    } catch (const std::exception& ex) {
        RAISE_PLUGIN_ERROR_AND_EXIT(ex.what(), plugin_interface);
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
