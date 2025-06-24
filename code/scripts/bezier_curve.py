import math
import numpy as np
import matplotlib.pyplot as plt

def bezier_curve_control(points):
    control_points = []
    for point in points:
        degrees = point[3]
        if degrees > 90:
            degrees = degrees - 180
        x = point[2] * math.cos(math.radians(degrees) )
        y = point[2] * math.sin(math.radians(degrees))
        control_points.append([point[0] - x, point[1] - y])
        control_points.append([point[0], point[1]])
        control_points.append([point[0] + x, point[1] + y])
    return np.array(control_points) 

def bezier_curve_point(points, n, p):
    sub_points = []
    for i in range(n):
        x = (points[i + 1][0] - points[i][0]) * p + points[i][0]
        y = (points[i + 1][1] - points[i][1]) * p + points[i][1]
        sub_points.append([x,y])
    if n == 1:
        return sub_points[0]
    else:
        return bezier_curve_point(sub_points, n - 1, p)
    
def bezier_curve_point_vaid(points, n, x):
    if x < points[0][0]:
        return False
    elif x > points[n][0]:
        return False
    return True

def bezier_curve(points, num=100):
    result = np.array
    control_points = bezier_curve_control(points)
    if len(control_points) >= 6 and len(control_points) % 3 == 0:
        lines = int((len(control_points) / 3)) - 1
        for i in range(lines):
            curve = np.zeros((num, 2))
            line_points = control_points[i * 3 + 1 : (i + 1) * 3 + 2]
            p_list = np.linspace(0, 1, num)
            for index, p in enumerate(p_list):
                xy = bezier_curve_point(line_points, 3, p)
                curve[index][0] = xy[0]
                curve[index][1] = xy[1]
            if i == 0:
                result = curve
            else:
                result = np.concatenate((result, curve[1:]))
    return control_points, np.array(result)

# 控制点（可以修改这些点来改变曲线形状）
points = np.array([
    [0, 0, 0.2, 0],      # 起点    
    [0.5, 0.5, 0.4, 70],
    [1, 1, 0.2, 0]       # 终点
])
# 计算贝塞尔曲线
control_points, curve = bezier_curve(points)
# 绘制结果
plt.figure(figsize=(8, 6))
plt.plot(control_points[:, 0], control_points[:, 1], 'ro-', label='Control Points')
plt.plot(curve[:, 0], curve[:, 1], 'b-', linewidth=2, label='Bezier Curve')
plt.legend()
plt.grid(True)
plt.title('Bezier Curve')
plt.xlabel('X')
plt.ylabel('Y')
plt.show()