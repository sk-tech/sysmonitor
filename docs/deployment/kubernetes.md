# Kubernetes Deployment Guide

This guide covers deploying SysMonitor on Kubernetes clusters.

## Architecture

SysMonitor on Kubernetes consists of:

- **Aggregator**: Single deployment for centralized metric collection
- **Agent**: DaemonSet running on every node
- **Dashboard**: Optional web UI deployment

## Prerequisites

- Kubernetes cluster (1.19+)
- `kubectl` configured
- Sufficient resources (1 CPU, 2GB RAM minimum)

## Quick Start

```bash
# Apply all manifests
kubectl apply -f k8s/

# Check deployment
kubectl get all -n sysmon

# Access dashboard
kubectl port-forward -n sysmon svc/sysmon-aggregator 9000:9000
```

## Manifests

### Namespace

`k8s/namespace.yaml`:

```yaml
apiVersion: v1
kind: Namespace
metadata:
  name: sysmon
  labels:
    name: sysmon
    app.kubernetes.io/name: sysmonitor
```

### ConfigMaps

`k8s/configmap-agent.yaml`:

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: sysmon-agent-config
  namespace: sysmon
data:
  config.yaml: |
    agent:
      hostname: ${NODE_NAME}
      tags:
        environment: kubernetes
        cluster: ${CLUSTER_NAME}
    
    publisher:
      aggregator_url: http://sysmon-aggregator:9000
      publish_interval_ms: 5000
      batch_size: 50
    
    storage:
      db_path: /var/lib/sysmon/metrics.db
      retention_days: 3
    
    collection:
      interval_ms: 1000
```

`k8s/configmap-aggregator.yaml`:

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: sysmon-aggregator-config
  namespace: sysmon
data:
  config.yaml: |
    server:
      port: 9000
      host: 0.0.0.0
    
    storage:
      db_path: /var/lib/sysmon/aggregator.db
      retention_days: 90
    
    alerts:
      enabled: true
      config_path: /etc/sysmon/alerts.yaml
```

### Aggregator Deployment

`k8s/aggregator-deployment.yaml`:

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: sysmon-aggregator
  namespace: sysmon
  labels:
    app: sysmon-aggregator
spec:
  replicas: 1
  selector:
    matchLabels:
      app: sysmon-aggregator
  template:
    metadata:
      labels:
        app: sysmon-aggregator
    spec:
      containers:
      - name: aggregator
        image: sysmonitor-aggregator:latest
        imagePullPolicy: IfNotPresent
        ports:
        - name: http
          containerPort: 9000
          protocol: TCP
        env:
        - name: SYSMON_PORT
          value: "9000"
        - name: CLUSTER_NAME
          value: "production"
        volumeMounts:
        - name: config
          mountPath: /etc/sysmon
          readOnly: true
        - name: data
          mountPath: /var/lib/sysmon
        resources:
          requests:
            memory: "512Mi"
            cpu: "250m"
          limits:
            memory: "2Gi"
            cpu: "1000m"
        livenessProbe:
          httpGet:
            path: /health
            port: 9000
          initialDelaySeconds: 10
          periodSeconds: 30
        readinessProbe:
          httpGet:
            path: /health
            port: 9000
          initialDelaySeconds: 5
          periodSeconds: 10
      volumes:
      - name: config
        configMap:
          name: sysmon-aggregator-config
      - name: data
        persistentVolumeClaim:
          claimName: sysmon-aggregator-pvc
```

### Aggregator Service

`k8s/aggregator-service.yaml`:

```yaml
apiVersion: v1
kind: Service
metadata:
  name: sysmon-aggregator
  namespace: sysmon
  labels:
    app: sysmon-aggregator
spec:
  type: ClusterIP
  ports:
  - port: 9000
    targetPort: 9000
    protocol: TCP
    name: http
  selector:
    app: sysmon-aggregator
```

### Agent DaemonSet

`k8s/agent-daemonset.yaml`:

```yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: sysmon-agent
  namespace: sysmon
  labels:
    app: sysmon-agent
spec:
  selector:
    matchLabels:
      app: sysmon-agent
  template:
    metadata:
      labels:
        app: sysmon-agent
    spec:
      hostNetwork: true
      hostPID: true
      hostIPC: true
      containers:
      - name: agent
        image: sysmonitor-agent:latest
        imagePullPolicy: IfNotPresent
        env:
        - name: NODE_NAME
          valueFrom:
            fieldRef:
              fieldPath: spec.nodeName
        - name: NODE_IP
          valueFrom:
            fieldRef:
              fieldPath: status.hostIP
        - name: CLUSTER_NAME
          value: "production"
        securityContext:
          privileged: true
        volumeMounts:
        - name: config
          mountPath: /etc/sysmon
          readOnly: true
        - name: proc
          mountPath: /host/proc
          readOnly: true
        - name: sys
          mountPath: /host/sys
          readOnly: true
        - name: data
          mountPath: /var/lib/sysmon
        resources:
          requests:
            memory: "128Mi"
            cpu: "100m"
          limits:
            memory: "512Mi"
            cpu: "500m"
      volumes:
      - name: config
        configMap:
          name: sysmon-agent-config
      - name: proc
        hostPath:
          path: /proc
      - name: sys
        hostPath:
          path: /sys
      - name: data
        emptyDir: {}
      tolerations:
      - effect: NoSchedule
        operator: Exists
      - effect: NoExecute
        operator: Exists
```

### Persistent Volume

`k8s/pvc-aggregator.yaml`:

```yaml
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: sysmon-aggregator-pvc
  namespace: sysmon
spec:
  accessModes:
    - ReadWriteOnce
  resources:
    requests:
      storage: 10Gi
  storageClassName: standard
```

### Ingress (Optional)

`k8s/ingress.yaml`:

```yaml
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: sysmon-ingress
  namespace: sysmon
  annotations:
    nginx.ingress.kubernetes.io/rewrite-target: /
    cert-manager.io/cluster-issuer: "letsencrypt-prod"
spec:
  tls:
  - hosts:
    - sysmon.example.com
    secretName: sysmon-tls
  rules:
  - host: sysmon.example.com
    http:
      paths:
      - path: /
        pathType: Prefix
        backend:
          service:
            name: sysmon-aggregator
            port:
              number: 9000
```

### ServiceMonitor (Prometheus)

`k8s/servicemonitor.yaml`:

```yaml
apiVersion: monitoring.coreos.com/v1
kind: ServiceMonitor
metadata:
  name: sysmon-aggregator
  namespace: sysmon
  labels:
    app: sysmon-aggregator
spec:
  selector:
    matchLabels:
      app: sysmon-aggregator
  endpoints:
  - port: http
    interval: 30s
    path: /metrics
```

## Deployment

### Create Namespace and ConfigMaps

```bash
kubectl apply -f k8s/namespace.yaml
kubectl apply -f k8s/configmap-agent.yaml
kubectl apply -f k8s/configmap-aggregator.yaml
```

### Deploy Aggregator

```bash
kubectl apply -f k8s/pvc-aggregator.yaml
kubectl apply -f k8s/aggregator-deployment.yaml
kubectl apply -f k8s/aggregator-service.yaml
```

### Deploy Agents

```bash
kubectl apply -f k8s/agent-daemonset.yaml
```

### Verify Deployment

```bash
# Check pods
kubectl get pods -n sysmon

# Check DaemonSet
kubectl get ds -n sysmon

# Check logs
kubectl logs -n sysmon -l app=sysmon-aggregator
kubectl logs -n sysmon -l app=sysmon-agent --tail=50
```

## Access Dashboard

### Port Forward

```bash
kubectl port-forward -n sysmon svc/sysmon-aggregator 9000:9000
# Open http://localhost:9000
```

### Ingress

```bash
kubectl apply -f k8s/ingress.yaml
# Access via https://sysmon.example.com
```

## Scaling

### Aggregator

```bash
# Scale up (if using StatefulSet)
kubectl scale deployment -n sysmon sysmon-aggregator --replicas=3
```

### Agent

Agents automatically scale via DaemonSet (one per node).

## Monitoring

### Resource Usage

```bash
# Check resource usage
kubectl top pods -n sysmon
kubectl top nodes

# Check metrics
kubectl get --raw /apis/metrics.k8s.io/v1beta1/namespaces/sysmon/pods
```

### Logs

```bash
# Aggregator logs
kubectl logs -n sysmon -l app=sysmon-aggregator -f

# Agent logs from specific node
kubectl logs -n sysmon -l app=sysmon-agent --field-selector spec.nodeName=worker-1 -f
```

## Upgrade

### Rolling Update

```bash
# Update image
kubectl set image deployment/sysmon-aggregator -n sysmon aggregator=sysmonitor-aggregator:v1.2.0

# Update DaemonSet
kubectl set image daemonset/sysmon-agent -n sysmon agent=sysmonitor-agent:v1.2.0

# Check rollout status
kubectl rollout status deployment/sysmon-aggregator -n sysmon
kubectl rollout status daemonset/sysmon-agent -n sysmon
```

### Rollback

```bash
# Rollback deployment
kubectl rollout undo deployment/sysmon-aggregator -n sysmon

# Check history
kubectl rollout history deployment/sysmon-aggregator -n sysmon
```

## Backup and Restore

### Backup

```bash
# Backup aggregator database
POD=$(kubectl get pod -n sysmon -l app=sysmon-aggregator -o jsonpath='{.items[0].metadata.name}')
kubectl exec -n sysmon $POD -- sqlite3 /var/lib/sysmon/aggregator.db ".backup /tmp/backup.db"
kubectl cp sysmon/$POD:/tmp/backup.db ./backup-$(date +%Y%m%d).db
```

### Restore

```bash
POD=$(kubectl get pod -n sysmon -l app=sysmon-aggregator -o jsonpath='{.items[0].metadata.name}')
kubectl cp ./backup.db sysmon/$POD:/tmp/restore.db
kubectl exec -n sysmon $POD -- sh -c "mv /tmp/restore.db /var/lib/sysmon/aggregator.db"
kubectl delete pod -n sysmon $POD  # Restart pod
```

## Troubleshooting

### Pods not starting

```bash
# Describe pod
kubectl describe pod -n sysmon -l app=sysmon-agent

# Check events
kubectl get events -n sysmon --sort-by='.lastTimestamp'
```

### Agent can't reach aggregator

```bash
# Test connectivity from agent
AGENT_POD=$(kubectl get pod -n sysmon -l app=sysmon-agent -o jsonpath='{.items[0].metadata.name}')
kubectl exec -n sysmon $AGENT_POD -- curl http://sysmon-aggregator:9000/health
```

### Permission issues

```bash
# Check security context
kubectl get pod -n sysmon $AGENT_POD -o yaml | grep -A 10 securityContext
```

## Helm Chart (Advanced)

Create `helm/Chart.yaml`:

```yaml
apiVersion: v2
name: sysmonitor
description: Cross-platform system monitoring
version: 1.0.0
appVersion: 1.0.0
```

Create `helm/values.yaml`:

```yaml
aggregator:
  replicas: 1
  image:
    repository: sysmonitor-aggregator
    tag: latest
  resources:
    requests:
      memory: 512Mi
      cpu: 250m
    limits:
      memory: 2Gi
      cpu: 1000m

agent:
  image:
    repository: sysmonitor-agent
    tag: latest
  resources:
    requests:
      memory: 128Mi
      cpu: 100m
    limits:
      memory: 512Mi
      cpu: 500m

storage:
  size: 10Gi
  storageClass: standard
```

Install with Helm:

```bash
helm install sysmonitor ./helm -n sysmon --create-namespace
```

## Uninstall

```bash
# Delete all resources
kubectl delete namespace sysmon

# Or individually
kubectl delete -f k8s/
```
