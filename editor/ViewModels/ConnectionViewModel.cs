using Avalonia;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels
{
    public partial class ConnectionViewModel : ObservableObject
    {
        [ObservableProperty]
        private NodeViewModel _sourceNode;

        [ObservableProperty]
        private NodeViewModel _targetNode;

        [ObservableProperty]
        private Point _startPoint;

        [ObservableProperty]
        private Point _endPoint;

        public ConnectionViewModel(NodeViewModel sourceNode, NodeViewModel targetNode)
        {
            _sourceNode = sourceNode;
            _targetNode = targetNode;
            UpdatePoints();
        }

        public void UpdatePoints()
        {
            // Right output pin center: Card X + 250, Y + 60
            StartPoint = new Point(SourceNode.X + 250, SourceNode.Y + 60);
            // Left input pin center: Target Card X + 10, Y + 60
            EndPoint = new Point(TargetNode.X + 10, TargetNode.Y + 60);
        }
    }
}
