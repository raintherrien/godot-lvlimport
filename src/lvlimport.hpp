#ifndef LVLIMPORT_TEST_HPP_
#define LVLIMPORT_TEST_HPP_

#include <godot_cpp/classes/node.hpp>

namespace godot {

class LVLImport : public Node {
	GDCLASS(LVLImport, Node)

protected:
	static void _bind_methods();
public:
	static void import_lvl();
};

}

#endif
