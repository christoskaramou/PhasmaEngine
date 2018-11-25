#include "../include/Node.h"

using namespace vm;

Node::Node()
{
	component_ptr = nullptr;
	parent = nullptr;
	children = {};
}

vm::Node::Node(std::any component_ptr, Node* parent, std::vector<Node*>& children)
{
	this->component_ptr = component_ptr;
	this->parent = parent;
	this->children = children;
}

Node::~Node()
{
	component_ptr.reset();
	parent = nullptr;
	children.clear();
}

Node * vm::Node::getParent()
{
	return parent;
}

std::vector<Node*>& vm::Node::getChildren()
{
	return children;
}

bool vm::Node::hasComponent()
{
	return component_ptr.has_value();
}

bool vm::Node::hasParent()
{
	return parent ? true : false;
}

bool vm::Node::hasChildren()
{
	return children.size() > 0 ? true : false;
}

bool vm::Node::hasValidChildren()
{
	for (auto& child : children)
		if (!child) return false;
	return true;
}

void vm::Node::setComponent(std::any component_ptr)
{
	this->component_ptr = component_ptr;
}

void vm::Node::removeComponent()
{
	component_ptr.reset();
}

void vm::Node::setParent(Node * parent)
{
	this->parent = parent;
}

void vm::Node::removeParent()
{
	parent = nullptr;
}

void vm::Node::addChild(Node * child)
{
	children.push_back(child);
}

void vm::Node::removeChild(Node * child)
{
	int i = 0;
	for (auto& ch : children) {
		if (ch = child)
			children.erase(children.begin() + i);
		i++;
	}
}

void vm::Node::addChildren(std::vector<Node*>& children)
{
	for (auto& child : children)
		this->children.push_back(child);
}

void vm::Node::removeChildren()
{
	children.clear();
}
