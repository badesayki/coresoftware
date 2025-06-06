//  Implementation of class PHNodeReset
//  Author: Matthias Messer

#include "PHNodeReset.h"

#include "PHDataNode.h"
#include "PHNode.h"
#include "PHObject.h"

#include <iostream>
#include <string>

void PHNodeReset::perform(PHNode* node)
{
  if (!node->getResetFlag())
  {
    return;
  }
  if (verbosity > 0)
  {
    std::cout << "PHNodeReset: Resetting " << node->getName() << std::endl;
  }
  if (node->getType() == "PHDataNode" || node->getType() == "PHIODataNode")
  {
    if (node->getObjectType() == "PHObject")
    {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
      (static_cast<PHDataNode<PHObject>*>(node))->getData()->Reset();
    }
  }
}
