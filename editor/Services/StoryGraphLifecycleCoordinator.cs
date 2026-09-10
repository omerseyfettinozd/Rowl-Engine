using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// Coordinates loading, saving, transactional rollback, start-node resolution,
    /// and runtime node synchronization for the story graph.
    /// </summary>
    public static class StoryGraphLifecycleCoordinator
    {
        /// <summary>
        /// Loads the story graph from full_story_graph.json via StoryGraphDocumentReader.
        /// Performs transactional rollback if reading, parsing, or hydration fails,
        /// leaving existing nodes and connections completely intact.
        /// </summary>
        public static bool LoadGraphWithRollback(
            string assetsPath,
            string assetsJsonPath,
            ObservableCollection<NodeViewModel> nodes,
            ObservableCollection<ConnectionViewModel> connections,
            PropertyChangedEventHandler onNodePropertyChanged,
            Action enforceSingleOutgoingWireRule,
            Action<NodeViewModel> selectNodeQuiet,
            Action<string>? log = null)
        {
            if (!StoryGraphDocumentReader.TryRead(
                    assetsPath,
                    assetsJsonPath,
                    out var document,
                    out var filePath,
                    out var readError))
            {
                if (!string.IsNullOrEmpty(readError))
                    log?.Invoke($"⚠️ Failed to read story graph: {readError}");
                return false;
            }

            var previousNodes = nodes.ToList();
            var previousConnections = connections.ToList();

            try
            {
                var parsedDocument = document!;
                using (parsedDocument)
                {
                    var loadResult = StoryGraphLoaderService.Load(parsedDocument);
                    if (!loadResult.Success)
                    {
                        if (!string.IsNullOrEmpty(loadResult.ErrorMessage))
                            log?.Invoke($"⚠️ Failed to load story graph: {loadResult.ErrorMessage}");
                        return false;
                    }

                    foreach (var warning in loadResult.Warnings)
                    {
                        log?.Invoke(warning);
                    }

                    nodes.Clear();
                    connections.Clear();

                    foreach (var node in loadResult.Nodes)
                    {
                        node.RefreshBitmaps();
                        node.PropertyChanged += onNodePropertyChanged;
                        nodes.Add(node);
                    }

                    foreach (var connection in loadResult.Connections)
                    {
                        connections.Add(connection);
                    }

                    enforceSingleOutgoingWireRule();

                    var startNode = ResolveStartNode(nodes, connections);
                    if (startNode != null)
                    {
                        selectNodeQuiet(startNode);
                    }

                    log?.Invoke($"📂 Loaded story graph from {filePath} ({nodes.Count} nodes, {connections.Count} connections, format v{loadResult.FormatVersion})");
                    return true;
                }
            }
            catch (Exception ex)
            {
                // Transactional rollback on error
                nodes.Clear();
                connections.Clear();
                foreach (var node in previousNodes) nodes.Add(node);
                foreach (var connection in previousConnections) connections.Add(connection);
                UpdateStartNodeState(nodes, connections);
                log?.Invoke($"⚠️ Failed to load story graph: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Saves the full story graph to disk atomically across Assets/ and Assets/json/.
        /// </summary>
        public static bool SaveFullGraph(
            string assetsPath,
            string assetsJsonPath,
            IEnumerable<NodeViewModel> nodes,
            IEnumerable<ConnectionViewModel> connections,
            ulong? startNodeId = null,
            Action<string>? log = null)
        {
            ulong effectiveStartId = startNodeId ?? ResolveStartNode(nodes, connections)?.Id ?? 101;
            return StoryGraphDocumentWriter.SaveFullStoryGraph(
                assetsPath,
                assetsJsonPath,
                nodes,
                connections,
                effectiveStartId,
                log);
        }

        /// <summary>
        /// Saves the preview active_story.json file for the current frame.
        /// </summary>
        public static bool SaveActiveStory(
            string assetsJsonPath,
            NodeViewModel? activeNode,
            IEnumerable<NodeViewModel> fallbackNodes,
            Action<string>? log = null)
        {
            var targetNode = activeNode ?? fallbackNodes.FirstOrDefault();
            return StoryGraphDocumentWriter.SaveActiveStory(
                assetsJsonPath,
                targetNode,
                log);
        }

        /// <summary>
        /// Saves both the full graph and active preview file, returning true if both succeeded.
        /// </summary>
        public static bool SaveProject(
            string assetsPath,
            string assetsJsonPath,
            IEnumerable<NodeViewModel> nodes,
            IEnumerable<ConnectionViewModel> connections,
            NodeViewModel? selectedNode,
            ulong? startNodeId = null,
            Action<string>? log = null)
        {
            bool fullSaved = SaveFullGraph(assetsPath, assetsJsonPath, nodes, connections, startNodeId, log);
            bool activeSaved = SaveActiveStory(assetsJsonPath, selectedNode, nodes, log);
            return fullSaved && activeSaved;
        }

        /// <summary>
        /// Updates the IsStartNode flag across all nodes based on in-degree zero or lowest ID.
        /// </summary>
        public static void UpdateStartNodeState(
            IEnumerable<NodeViewModel> nodes,
            IEnumerable<ConnectionViewModel> connections)
        {
            var nodeList = nodes as IList<NodeViewModel> ?? nodes.ToList();
            var connList = connections as IList<ConnectionViewModel> ?? connections.ToList();

            var startNode = nodeList.FirstOrDefault(n => !connList.Any(c => c.TargetNode == n))
                            ?? nodeList.OrderBy(n => n.Id).FirstOrDefault();

            foreach (var node in nodeList)
            {
                node.IsStartNode = (node == startNode);
            }
        }

        /// <summary>
        /// Resolves the candidate start node (first marked IsStartNode, or in-degree zero, or lowest ID).
        /// </summary>
        public static NodeViewModel? ResolveStartNode(
            IEnumerable<NodeViewModel> nodes,
            IEnumerable<ConnectionViewModel> connections)
        {
            var nodeList = nodes as IList<NodeViewModel> ?? nodes.ToList();
            var connList = connections as IList<ConnectionViewModel> ?? connections.ToList();

            return nodeList.FirstOrDefault(n => n.IsStartNode)
                   ?? nodeList.FirstOrDefault(n => !connList.Any(c => c.TargetNode == n))
                   ?? nodeList.OrderBy(n => n.Id).FirstOrDefault();
        }

        /// <summary>
        /// Syncs the editor's selected node with the native runtime's current active node ID.
        /// </summary>
        public static NodeViewModel? SyncEditorToRuntimeNode(
            EngineHost engineHost,
            IEnumerable<NodeViewModel> nodes,
            Action<NodeViewModel> selectNodeQuiet)
        {
            if (engineHost == null || !engineHost.IsInitialized) return null;
            ulong nodeId = engineHost.GetCurrentNodeId();
            var matchedNode = nodes.FirstOrDefault(item => item.Id == nodeId);
            if (matchedNode != null)
            {
                selectNodeQuiet(matchedNode);
            }
            return matchedNode;
        }
    }
}
