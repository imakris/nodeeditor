Serialization
=============

Save and load graphs as JSON.

JSON Structure
--------------

A saved graph contains nodes and connections:

.. code-block:: json

   {
     "nodes": [
       {
         "id": 0,
         "position": {"x": 100, "y": 50},
         "internal-data": {
           "model-name": "NumberSource",
           "value": 42.0
         }
       },
       {
         "id": 1,
         "position": {"x": 300, "y": 50},
         "internal-data": {
           "model-name": "Display"
         }
       }
     ],
     "connections": [
       {
         "outNodeId": 0,
         "outPortIndex": 0,
         "inNodeId": 1,
         "inPortIndex": 0
       }
     ]
   }

Using DataFlowGraphModel
------------------------

``DataFlowGraphModel`` implements ``Serializable``:

.. code-block:: cpp

   DataFlowGraphModel model(registry);
   DataFlowGraphicsScene scene(model);

   // Save to JSON object
   QJsonObject json = model.save();

   // Save to file
   QFile file("graph.json");
   file.open(QIODevice::WriteOnly);
   file.write(QJsonDocument(json).toJson());

   // Replace the live document through the checked transactional scene flow.
   // It returns false for file, JSON, validation, or reconstruction failures.
   if (!scene.load()) {
       qWarning() << "Graph was not loaded";
   }

``DataFlowGraphModel::load()`` is a lower-level building block. It requires an
empty model, throws ``std::logic_error`` for an invalid object, and rolls back
nodes, connections, and the id allocator when reconstruction fails. It is not a
live-model document replacement API. ``DataFlowGraphicsScene::load()`` parses
with ``QJsonParseError``, stages the full document, preserves the live graph and
undo history on rejection, and clears and marks the undo stack clean only after
a successful replacement.

Using DataFlowGraphicsScene
---------------------------

The scene provides file dialogs:

.. code-block:: cpp

   DataFlowGraphicsScene scene(model);

   // Opens save dialog, returns true on success
   if (scene.save()) {
       qDebug() << "Saved!";
   }

   // Opens load dialog
   scene.load();

   // React to load completion
   connect(&scene, &DataFlowGraphicsScene::sceneLoaded, [&view]() {
       view.centerScene();
   });

Custom Model Serialization
--------------------------

For custom ``AbstractGraphModel`` subclasses, implement ``saveNode()`` and ``loadNode()``:

.. code-block:: cpp

   QJsonObject MyModel::saveNode(NodeId nodeId) const
   {
       QJsonObject json;

       // Required: ID
       json["id"] = static_cast<qint64>(nodeId);

       // Required: Position
       QPointF pos = nodeData(nodeId, NodeRole::Position).toPointF();
       json["position"] = QJsonObject{{"x", pos.x()}, {"y", pos.y()}};

       // Optional: Your custom data
       json["internal-data"] = QJsonObject{
           {"type", _nodeTypes[nodeId]},
           {"custom-field", _customData[nodeId]}
       };

       return json;
   }

   void MyModel::loadNode(QJsonObject const& json)
   {
       NodeId nodeId = static_cast<NodeId>(json["id"].toInt());

       // Ensure unique IDs
       _nextId = std::max(_nextId, nodeId + 1);

       // Create node
       _nodes.insert(nodeId);
       emit nodeCreated(nodeId);

       // Restore position
       QJsonObject posJson = json["position"].toObject();
       setNodeData(nodeId, NodeRole::Position,
                   QPointF(posJson["x"].toDouble(), posJson["y"].toDouble()));

       // Restore custom data
       QJsonObject internal = json["internal-data"].toObject();
       _nodeTypes[nodeId] = internal["type"].toString();
       _customData[nodeId] = internal["custom-field"].toString();
   }

Then implement full save/load:

.. code-block:: cpp

   QJsonObject MyModel::save() const
   {
       QJsonArray nodesArray;
       for (NodeId nodeId : allNodeIds()) {
           nodesArray.append(saveNode(nodeId));
       }

       QJsonArray connectionsArray;
       for (NodeId nodeId : allNodeIds()) {
           for (auto& conn : allConnectionIds(nodeId)) {
               // Avoid duplicates: only save from output side
               if (conn.outNodeId == nodeId) {
                   connectionsArray.append(toJson(conn));
               }
           }
       }

       return QJsonObject{
           {"nodes", nodesArray},
           {"connections", connectionsArray}
       };
   }

   bool MyModel::load(QJsonObject const& json)
   {
       if (!json["nodes"].isArray() || !json["connections"].isArray()) {
           return false;
       }

       std::vector<QJsonObject> nodes;
       for (QJsonValue const nodeValue : json["nodes"].toArray()) {
           if (!nodeValue.isObject()) {
               return false;
           }
           nodes.push_back(nodeValue.toObject());
       }

       std::vector<ConnectionId> connections;
       for (QJsonValue const connectionValue : json["connections"].toArray()) {
           if (!connectionValue.isObject()) {
               return false;
           }

           ConnectionId connection;
           if (!tryFromJson(connectionValue.toObject(), connection)) {
               return false;
           }
           connections.push_back(connection);
       }

       // This model-owned preflight validates required node fields, unique IDs,
       // endpoint existence, port ranges, and connection policy without mutation.
       if (!validateGraphPayload(nodes, connections)) {
           return false;
       }

       std::vector<NodeId> existingNodes(allNodeIds().begin(), allNodeIds().end());
       for (NodeId id : existingNodes) {
           deleteNode(id);
       }
       for (QJsonObject const& node : nodes) {
           loadNode(node);
       }
       for (ConnectionId const connection : connections) {
           addConnection(connection);
       }

       return true;
   }

Validation is complete before the first deletion or insertion, so a rejected
external payload leaves the current graph and its mutation signals unchanged.

The ``dynamic_ports`` teaching example goes further and prepares its complete
replacement state, including its embedded widgets and connection index, before
swapping it into the live model. Its operational contract is intentionally
finite because every port creates a layout and two buttons: at most 128 nodes,
32 ports per node, 256 total ports, 256 serialized connections, and 1 MiB of
serialized JSON. The file action checks the file size before a bounded read,
and the byte loader checks again before JSON parsing. The same graph limits are
enforced by interactive and programmatic edits, so every state the example
produces can be saved and loaded again.

NodeDelegateModel Serialization
-------------------------------

Delegates can save custom state:

.. code-block:: cpp

   class MyNode : public NodeDelegateModel
   {
   public:
       QJsonObject save() const override
       {
           QJsonObject json = NodeDelegateModel::save();
           json["my-value"] = _spinBox->value();
           return json;
       }

       void load(QJsonObject const& json) override
       {
           NodeDelegateModel::load(json);
           _spinBox->setValue(json["my-value"].toDouble());
       }

   private:
       QDoubleSpinBox* _spinBox;
   };

Connection ID Utilities
-----------------------

Helper functions in ``ConnectionIdUtils.hpp``:

.. code-block:: cpp

   #include <QtNodes/ConnectionIdUtils>

   // Convert to/from JSON. Parsing malformed or sentinel values returns false.
   QJsonObject json = QtNodes::toJson(connectionId);
   ConnectionId parsed;
   if (!QtNodes::tryFromJson(json, parsed)) {
       // Reject the containing payload.
   }

Complete Save/Load Example
--------------------------

.. code-block:: cpp

   // In your main window
   QAction* saveAction = fileMenu->addAction("Save", [&]() {
       QString path = QFileDialog::getSaveFileName(
           this, "Save Graph", "", "JSON Files (*.json)");
       if (path.isEmpty()) return;

       QFile file(path);
       if (file.open(QIODevice::WriteOnly)) {
           QJsonDocument doc(model.save());
           file.write(doc.toJson(QJsonDocument::Indented));
       }
   });

   QAction* loadAction = fileMenu->addAction("Load", [&]() {
       QString path = QFileDialog::getOpenFileName(
           this, "Load Graph", "", "JSON Files (*.json)");
       if (path.isEmpty()) return;

       QFile file(path);
       if (file.open(QIODevice::ReadOnly)) {
           QJsonParseError error{};
           QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
           if (error.error == QJsonParseError::NoError && doc.isObject()
               && model.load(doc.object())) {
               view.centerScene();
           }
       }
   });

.. seealso::

   - ``examples/calculator/`` -- Save/load implementation
   - :doc:`undo-redo` -- Undo uses serialization internally
