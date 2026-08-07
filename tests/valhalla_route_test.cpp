// Test minimo de ruta con Valhalla, calcado de lo que hace OSM Scout Server.
//
// Existe para dos cosas: comprobar que los tiles ya descargados en Ubuntu Touch
// cargan con la version 3.4.0 que fija pkg-valhalla-lite, y servir de banco de
// pruebas de la cross-compilacion a Android — el mismo fuente se compilara con
// el NDK en la fase 1.
//
// Usa actor_t, que es la API que empaqueta pkg-valhalla-lite y la que usa
// valhallamaster.cpp del servidor.
//
//   uso: valhalla_route_test <plantilla.json> <dir_tiles> <lat1> <lon1> <lat2> <lon2>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <valhalla/tyr/actor.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

// La plantilla del servidor lleva marcadores en vez de valores; se sustituyen
// igual que en valhallamaster.cpp (los nombres salen de valhallamaster.h).
void replace_all(std::string &s, const std::string &from, const std::string &to) {
  for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size()))
    s.replace(p, from.size(), to);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 7) {
    std::cerr << "uso: " << argv[0]
              << " <plantilla.json> <dir_tiles> <lat1> <lon1> <lat2> <lon2>\n";
    return 2;
  }

  const std::string template_path = argv[1];
  const std::string tile_dir = argv[2];

  std::ifstream fin(template_path);
  if (!fin) {
    std::cerr << "no se pudo abrir la plantilla " << template_path << "\n";
    return 1;
  }
  std::stringstream buf;
  buf << fin.rdbuf();
  std::string conf = buf.str();

  replace_all(conf, "VALHALLA_TILE_DIRECTORY", tile_dir);
  replace_all(conf, "MAXIMAL_CACHE_SIZE", std::to_string(64L * 1024 * 1024));
  replace_all(conf, "LIMIT_MAX_DISTANCE_AUTO", "5000000");
  replace_all(conf, "LIMIT_MAX_DISTANCE_BICYCLE", "500000");
  replace_all(conf, "LIMIT_MAX_DISTANCE_PEDESTRIAN", "250000");

  boost::property_tree::ptree pt;
  try {
    std::stringstream cs(conf);
    boost::property_tree::read_json(cs, pt);
  } catch (const std::exception &e) {
    std::cerr << "configuracion invalida: " << e.what() << "\n";
    return 1;
  }

  // tile_extract apunta a un .tar que aqui no existe: los tiles estan sueltos en
  // el directorio. Si se deja, Valhalla intenta el tar primero y no encuentra nada.
  pt.get_child("mjolnir").erase("tile_extract");

  std::ostringstream req;
  req << "{\"locations\":[{\"lat\":" << argv[3] << ",\"lon\":" << argv[4] << "},"
      << "{\"lat\":" << argv[5] << ",\"lon\":" << argv[6] << "}],"
      << "\"costing\":\"auto\","
      << "\"directions_options\":{\"units\":\"kilometers\",\"language\":\"es-ES\"}}";

  try {
    valhalla::tyr::actor_t actor(pt, true);
    const std::string res = actor.route(req.str());

    // No se vuelca el JSON entero, que son cientos de KB de geometria: basta con
    // saber que hay maniobras y cuanto mide la ruta.
    std::cout << "RUTA OK, " << res.size() << " bytes de respuesta\n";
    const size_t p = res.find("\"length\":");
    if (p != std::string::npos)
      std::cout << "primer length -> " << res.substr(p, 40) << "\n";
    const size_t s = res.find("\"summary\":");
    if (s != std::string::npos)
      std::cout << "summary -> " << res.substr(s, 200) << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FALLO al calcular la ruta: " << e.what() << "\n";
    return 1;
  }
}
