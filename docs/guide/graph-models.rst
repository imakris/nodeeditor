Graph Models
============

The graph model is the core of your application. It stores nodes, connections,
and all associated data. This guide covers implementing your own model by
subclassing ``AbstractGraphModel``.

.. tip::

   If you want automatic data propagation between nodes, see :doc:`data-flow`
   instead. Use a custom graph model when you need full control over graph logic.

AbstractGraphModel Overview
---------------------------

Your model must implement these pure virtual methods:

.. code-block:: cpp

   class MyGraphModel : public QtNodes::AbstractGraphModel
   {
   public:
       // Node queries
       NodeIdSet const &allNodeIds() const override;
       bool nodeExists(NodeId) const override;
       QVariant nodeData(NodeId, NodeRole) const override;
       bool setNodeData(NodeId, NodeRole, QVariant) override;

       // Connection queries
       ConnectionIdSet const &allConnectionIds(NodeId) const override;
       ConnectionIdSet const &connections(NodeId, PortType, PortIndex) const override;
       bool connectionExists(ConnectionId) const override;
       bool connectionPossible(
           ConnectionId,
           std::vector<ConnectionId> const &replacedConnectionIds) const override;

       // Mutations
       NodeId addNode(QString nodeType) override;
       void addConnection(ConnectionId) override;
       bool deleteNode(NodeId) override;
       bool deleteConnection(ConnectionId) override;
       std::unique_ptr<ConnectionReplacementTransaction>
       prepareConnectionReplacement(
           std::vector<ConnectionId> const &removedConnectionIds,
           std::vector<ConnectionId> const &addedConnectionIds) noexcept override;

       // Port queries
       QVariant portData(NodeId, PortType, PortIndex, PortRole) const override;
       bool setPortData(NodeId, PortType, PortIndex, QVariant, PortRole) override;

   protected:
       // ID generation
       NodeId newNodeId() override;
   };

Implementing Node Management
----------------------------

**ID Generation**

Generate unique IDs. A simple counter works:

.. code-block:: cpp

   NodeId MyGraphModel::newNodeId()
   {
       return _nextId++;
   }

**Adding Nodes**

Store the node and emit the signal:

.. code-block:: cpp

   NodeId MyGraphModel::addNode(QString nodeType)
   {
       NodeId id = newNodeId();
       _nodes.insert(id);
       _nodeTypes[id] = nodeType;
       _nodePositions[id] = QPointF(0, 0);

       emit nodeCreated(id);  // Required!
       return id;
   }

**Deleting Nodes**

Remove connections first, then the node:

.. code-block:: cpp

   bool MyGraphModel::deleteNode(NodeId nodeId)
   {
       if (!nodeExists(nodeId))
           return false;

       // Remove all connections involving this node
       std::vector<ConnectionId> attachedConnections;
       auto const &connections = allConnectionIds(nodeId);
       attachedConnections.reserve(connections.size());
       for (auto const &conn : connections) {
           attachedConnections.push_back(conn);
       }
       for (auto const &conn : attachedConnections) {
           deleteConnection(conn);
       }

       _nodes.erase(nodeId);
       emit nodeDeleted(nodeId);  // Required!
       return true;
   }

NodeRole Reference
------------------

Implement ``nodeData()`` and ``setNodeData()`` to provide node information:

.. list-table::
   :widths: 20 15 65
   :header-rows: 1

   * - Role
     - Type
     - Description
   * - ``Type``
     - ``QString``
     - Node type identifier (e.g., "AddNode", "ImageFilter")
   * - ``Position``
     - ``QPointF``
     - Position on the canvas
   * - ``Size``
     - ``QSize``
     - Size hint for embedded widgets
   * - ``Caption``
     - ``QString``
     - Display name shown on the node
   * - ``CaptionVisible``
     - ``bool``
     - Whether to show the caption
   * - ``Style``
     - ``QVariantMap``
     - Per-node style overrides (JSON)
   * - ``InternalData``
     - ``QVariantMap``
     - Custom data for serialization
   * - ``InPortCount``
     - ``unsigned int``
     - Number of input ports
   * - ``OutPortCount``
     - ``unsigned int``
     - Number of output ports
   * - ``Widget``
     - ``QWidget*``
     - Embedded widget (or nullptr)
   * - ``ValidationState``
     - ``NodeValidationState``
     - Current validation state
   * - ``ProcessingStatus``
     - ``NodeProcessingStatus``
     - Current processing status

**Example implementation:**

.. code-block:: cpp

   QVariant MyGraphModel::nodeData(NodeId nodeId, NodeRole role) const
   {
       switch (role) {
       case NodeRole::Type:
           return _nodeTypes.value(nodeId);

       case NodeRole::Position:
           return _nodePositions.value(nodeId);

       case NodeRole::Caption:
           return QString("Node %1").arg(nodeId);

       case NodeRole::InPortCount:
           return 2u;  // All nodes have 2 inputs

       case NodeRole::OutPortCount:
           return 1u;  // All nodes have 1 output

       default:
           return {};
       }
   }

Implementing Connections
------------------------

**Connection Queries**

Return connections filtered by node and port:

.. code-block:: cpp

   AbstractGraphModel::ConnectionIdSet const &
   MyGraphModel::connections(NodeId nodeId, PortType portType, PortIndex portIndex) const
   {
       return _connectionIndex.connections(nodeId, portType, portIndex);
   }

**Connection Validation**

Control what connections are allowed:

.. code-block:: cpp

   bool MyGraphModel::connectionPossible(
       ConnectionId conn,
       std::vector<ConnectionId> const& replacedConnectionIds) const
   {
       auto isReplaced = [&](ConnectionId existing) {
           return std::find(replacedConnectionIds.begin(),
                            replacedConnectionIds.end(),
                            existing) != replacedConnectionIds.end();
       };

       // Nodes must exist
       if (!nodeExists(conn.inNodeId) || !nodeExists(conn.outNodeId))
           return false;

       // No self-connections
       if (conn.inNodeId == conn.outNodeId)
           return false;

       // No duplicate connections
       if (connectionExists(conn) && !isReplaced(conn))
           return false;

       // Capacity checks must count only connections that will remain.
       auto const& inputConnections =
           connections(conn.inNodeId, PortType::In, conn.inPortIndex);
       auto remainingInputCount = std::count_if(
           inputConnections.begin(), inputConnections.end(),
           [&](ConnectionId existing) { return !isReplaced(existing); });
       if (remainingInputCount != 0)
           return false;

       // Custom logic: check port compatibility, etc.
       return true;
   }

``replacedConnectionIds`` contains the existing edges that an atomic
one-output replacement will remove. Validation must treat those exact edges as
absent without changing the model; ordinary callers pass an empty vector
explicitly as ``{}``.

**Atomic Connection Replacement**

``prepareConnectionReplacement()`` is called before a single-output rewire can
enter undo history. It must perform every fallible admission and graph-storage
operation up front: verify that every removed id exists and is detachable, call
``connectionPossible()`` with that exact removed set, allocate the alternate
storage state, and construct the transaction. It returns null without mutation
or notification on any failure:

.. code-block:: cpp

   std::unique_ptr<ConnectionReplacementTransaction>
   MyGraphModel::prepareConnectionReplacement(
       std::vector<ConnectionId> const& removed,
       std::vector<ConnectionId> const& added) noexcept
   {
       try {
           if (added.size() != 1)
               return {};
           for (ConnectionId id : removed) {
               if (!connectionExists(id) || !detachPossible(id))
                   return {};
           }
           if (!connectionPossible(added.front(), removed))
               return {};

           auto prepared = _connectionIndex.preparedReplacement(removed, added);
           if (!prepared)
               return {};

           auto publishOne = [](auto&& notification) {
               try {
                   notification();
               } catch (std::exception const& error) {
                   qWarning() << "Connection replacement notification failed:"
                              << error.what();
               } catch (...) {
                   qWarning() << "Connection replacement notification failed"
                                 " with an unknown exception";
               }
           };
           auto publish = [this, publishOne](auto const& deleted,
                                             auto const& created) {
               // The transaction swaps complete storage before this callback.
               for (ConnectionId id : deleted)
                   publishOne([&] { emit connectionDeleted(id); });
               for (ConnectionId id : created)
                   publishOne([&] { emit connectionCreated(id); });
           };
           using Transaction =
               ConnectionIdIndexReplacementTransaction<decltype(publish)>;
           return std::make_unique<Transaction>(_connectionIndex,
                                                std::move(*prepared),
                                                removed,
                                                added,
                                                std::move(publish));
       } catch (...) {
           return {};
       }
   }

The transaction owns the inactive prebuilt state; the model still has one active
topology index. Its non-refusing ``undo()`` and ``redo()`` operations only swap
complete states. ``publishUndo()`` and ``publishRedo()`` are separate post-swap
operations for signals, delegate callbacks, and data delivery, which may be
fallible. Constructing the transaction, including moving its publisher into
place, is also fallible preparation and belongs inside the method's catch
boundary. The transaction constructor is deliberately not ``noexcept``.

A publisher must diagnose and contain each individual signal or callback
failure and continue with later deletions and creations. The command boundary
also contains and warns on an unexpected publisher exception as a last resort,
so an exception cannot terminate replay or make undo history disagree with
topology.

Topology replay does not validate, allocate graph storage, return a status, or
consult a refusal flag. Do not implement it by calling ``deleteConnection()``
and ``addConnection()`` in a loop: an individual failure or observer callback
would expose a partial topology.

PortRole Reference
------------------

Implement ``portData()`` for port-specific information:

.. list-table::
   :widths: 25 15 60
   :header-rows: 1

   * - Role
     - Type
     - Description
   * - ``Data``
     - ``std::shared_ptr<NodeData>``
     - The actual data at this port
   * - ``DataType``
     - ``NodeDataType``
     - Type descriptor for compatibility checks
   * - ``ConnectionPolicy``
     - ``ConnectionPolicy``
     - ``One`` (single connection) or ``Many``
   * - ``Caption``
     - ``QString``
     - Port label text
   * - ``CaptionVisible``
     - ``bool``
     - Whether to show the label

.. note::

   ``allNodeIds()``, ``allConnectionIds()``, and ``connections()`` return
   references to storage owned by the model. Implementations must keep those
   containers alive for the duration of the call site rather than constructing
   and returning temporaries.

Required Signals
----------------

Your model **must** emit these signals at the appropriate times:

.. code-block:: cpp

   // After adding a node
   emit nodeCreated(nodeId);

   // After removing a node
   emit nodeDeleted(nodeId);

   // After node data changes (caption, style, etc.)
   emit nodeUpdated(nodeId);

   // After position changes specifically
   emit nodePositionUpdated(nodeId);

   // After adding a connection
   emit connectionCreated(connectionId);

   // After removing a connection
   emit connectionDeleted(connectionId);

.. warning::

   Forgetting to emit signals will cause the view to become out of sync
   with your model.

Serialization Support
---------------------

Override ``saveNode()`` and ``loadNode()`` to support save/load:

.. code-block:: cpp

   QJsonObject MyGraphModel::saveNode(NodeId nodeId) const
   {
       QJsonObject json;
       json["id"] = static_cast<int>(nodeId);
       json["type"] = _nodeTypes[nodeId];

       QJsonObject pos;
       pos["x"] = _nodePositions[nodeId].x();
       pos["y"] = _nodePositions[nodeId].y();
       json["position"] = pos;

       return json;
   }

See :doc:`serialization` for complete save/load implementation.

Complete Example
----------------

See ``examples/simple_graph_model/`` for a complete, working implementation
of a custom graph model.

.. seealso::

   - :doc:`data-flow` -- For automatic data propagation
   - :doc:`/api/classes` -- Full API reference
