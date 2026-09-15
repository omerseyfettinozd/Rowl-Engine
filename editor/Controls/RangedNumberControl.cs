using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Layout;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// Faz 4 Dilim 4 — slider + numeric box bound to one double. Built from
    /// an <see cref="Services.Inspector.InspectorFieldDescriptor"/> range
    /// (min/max/step); both editors write the same TwoWay value, so schema
    /// limits apply wherever the control is used.
    /// </summary>
    public class RangedNumberControl : UserControl
    {
        public static readonly StyledProperty<double> ValueProperty =
            AvaloniaProperty.Register<RangedNumberControl, double>(
                nameof(Value), defaultBindingMode: BindingMode.TwoWay);

        public static readonly StyledProperty<double> MinimumProperty =
            AvaloniaProperty.Register<RangedNumberControl, double>(nameof(Minimum));

        public static readonly StyledProperty<double> MaximumProperty =
            AvaloniaProperty.Register<RangedNumberControl, double>(nameof(Maximum), 100.0);

        public static readonly StyledProperty<double> StepProperty =
            AvaloniaProperty.Register<RangedNumberControl, double>(nameof(Step), 1.0);

        public double Value
        {
            get => GetValue(ValueProperty);
            set => SetValue(ValueProperty, value);
        }

        public double Minimum
        {
            get => GetValue(MinimumProperty);
            set => SetValue(MinimumProperty, value);
        }

        public double Maximum
        {
            get => GetValue(MaximumProperty);
            set => SetValue(MaximumProperty, value);
        }

        public double Step
        {
            get => GetValue(StepProperty);
            set => SetValue(StepProperty, value);
        }

        public RangedNumberControl()
        {
            var grid = new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("*, Auto"),
            };
            var slider = new Slider();
            slider.Bind(Slider.ValueProperty, new Binding(nameof(Value)) { Source = this, Mode = BindingMode.TwoWay });
            slider.Bind(Slider.MinimumProperty, new Binding(nameof(Minimum)) { Source = this });
            slider.Bind(Slider.MaximumProperty, new Binding(nameof(Maximum)) { Source = this });
            var number = new NumericUpDown { Width = 84, Margin = new Thickness(6, 0, 0, 0) };
            number.Bind(NumericUpDown.ValueProperty, new Binding(nameof(Value)) { Source = this, Mode = BindingMode.TwoWay });
            number.Bind(NumericUpDown.MinimumProperty, new Binding(nameof(Minimum)) { Source = this });
            number.Bind(NumericUpDown.MaximumProperty, new Binding(nameof(Maximum)) { Source = this });
            number.Bind(NumericUpDown.IncrementProperty, new Binding(nameof(Step)) { Source = this });
            grid.Children.Add(slider);
            Grid.SetColumn(slider, 0);
            grid.Children.Add(number);
            Grid.SetColumn(number, 1);
            Content = grid;
            UpdateSliderStep();
        }

        static RangedNumberControl()
        {
            StepProperty.Changed.AddClassHandler<RangedNumberControl>((c, _) => c.UpdateSliderStep());
        }

        private void UpdateSliderStep()
        {
            if (Content is Grid grid && grid.Children[0] is Slider slider)
            {
                slider.SmallChange = Step;
                slider.LargeChange = Step * 3;
            }
        }
    }
}
