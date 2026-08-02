using Avalonia;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels
{
    public partial class ConnectionViewModel : ObservableObject
    {
        [ObservableProperty]
        private NodeViewModel? _sourceNode;

        [ObservableProperty]
        private NodeViewModel? _targetNode;

        [ObservableProperty]
        private Point _startPoint;

        [ObservableProperty]
        private Point _endPoint;

        public ConnectionViewModel(NodeViewModel? sourceNode, NodeViewModel? targetNode)
        {
            _sourceNode = sourceNode;
            _targetNode = targetNode;
            UpdatePoints();
        }

        partial void OnSourceNodeChanged(NodeViewModel? value)
        {
            UpdatePoints();
            // Subscribe to source node position changes
            if (value != null)
            {
                value.PropertyChanged += (s, e) =>
                {
                    if (e.PropertyName == nameof(NodeViewModel.X) || e.PropertyName == nameof(NodeViewModel.Y))
                        UpdatePoints();
                };
            }
        }

        partial void OnTargetNodeChanged(NodeViewModel? value)
        {
            UpdatePoints();
            // Subscribe to target node position changes
            if (value != null)
            {
                value.PropertyChanged += (s, e) =>
                {
                    if (e.PropertyName == nameof(NodeViewModel.X) || e.PropertyName == nameof(NodeViewModel.Y))
                        UpdatePoints();
                };
            }
        }

        public void UpdatePoints()
        {
            if (SourceNode == null || TargetNode == null) return;
            // Right output pin center: Card X + 250, Y + 60
            StartPoint = new Point(SourceNode.X + 250, SourceNode.Y + 60);
            // Left input pin center: Target Card X + 10, Y + 60
            EndPoint = new Point(TargetNode.X + 10, TargetNode.Y + 60);
        }
    }
}