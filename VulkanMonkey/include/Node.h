#pragma once
#include "Math.h"
#include <vector>
#include <any>

namespace vm {

	struct Transform {
		vec3 position;
		quat rotation;
		vec3 scale;
		mat4 matrix;
	};

	struct Node
	{
		Node();
		Node(std::any component_ptr, Node* parent, std::vector<Node*>& children);
		~Node();

		template<typename T>
		T& getComponent() { assert(component_ptr.has_value()); return *(std::any_cast<T*>(component_ptr)); }
		Node* getParent();
		std::vector<Node*>& getChildren();
		bool hasComponent();
		bool hasParent();
		bool hasChildren();
		bool hasValidChildren();
		void setComponent(std::any component_ptr);
		void removeComponent();
		void setParent(Node* parent);
		void removeParent();
		void addChild(Node* child);
		void removeChild(Node* child);
		void addChildren(std::vector<Node*>& children);
		void removeChildren();

	private:
		Transform transform;
		std::any component_ptr;
		Node* parent;
		std::vector<Node*> children;
	};
}
