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

        /// <summary>Stable ChoiceOption ID when this cable represents a button route.</summary>
        [ObservableProperty]
        private string _optionId = string.Empty;

        public ConnectionViewModel(NodeViewModel? sourceNode, NodeViewModel? targetNode, string optionId = "")
        {
            _sourceNode = sourceNode;
            _targetNode = targetNode;
            _optionId = optionId;
            UpdatePoints();
        }

        partial void OnSourceNodeChanged(NodeViewModel? value)
        {
            UpdatePoints();
        }

        partial void OnTargetNodeChanged(NodeViewModel? value)
        {
            UpdatePoints();
        }

        public void UpdatePoints()
        {
            if (SourceNode == null || TargetNode == null) return;
            // Choice edges begin at the exact option port, not a generic node output.
            StartPoint = new Point(SourceNode.X + 265, SourceNode.Y + SourceNode.GetOutputPortY(OptionId));
            // Left input pin center: Target Card X + 10, Y + 60
            EndPoint = new Point(TargetNode.X + 10, TargetNode.Y + 60);
        }
    }
}
