#include "Node.h"

using namespace vm;

Node::Node()
{
	component_ptr = nullptr;
	parent = nullptr;
	children = {};
}

Node::Node(std::any component_ptr, Node* parent, std::vector<Node*>& children)
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

Node * Node::getParent()
{
	return parent;
}

std::vector<Node*>& Node::getChildren()
{
	return children;
}

bool Node::hasComponent()
{
	return component_ptr.has_value();
}

bool Node::hasParent()
{
	return parent ? true : false;
}

bool Node::hasChildren()
{
	return children.size() > 0 ? true : false;
}

bool Node::hasValidChildren()
{
	for (auto& child : children)
		if (!child) return false;
	return true;
}

void Node::setComponent(std::any component_ptr)
{
	this->component_ptr = component_ptr;
}

void Node::removeComponent()
{
	component_ptr.reset();
}

void Node::setParent(Node * parent)
{
	this->parent = parent;
}

void Node::removeParent()
{
	parent = nullptr;
}

void Node::addChild(Node * child)
{
	children.push_back(child);
}

void Node::removeChild(Node * child)
{
	int i = 0;
	for (auto& ch : children) {
		if (ch = child)
			children.erase(children.begin() + i);
		i++;
	}
}

void Node::addChildren(std::vector<Node*>& children)
{
	for (auto& child : children)
		this->children.push_back(child);
}

void Node::removeChildren()
{
	children.clear();
}
